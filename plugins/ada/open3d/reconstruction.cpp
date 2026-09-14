#include "reconstruction.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <open3d/core/CUDAUtils.h>
#include <open3d/t/geometry/Image.h>
#include <open3d/t/geometry/VoxelBlockGrid.h>
#include <stdexcept>
#include <utility>
#include <vector>

namespace o3c = open3d::core;
namespace o3g = open3d::t::geometry;

namespace ILLIXR::ada_open3d {
struct reconstructor::implementation {
    const o3c::Device device{"CUDA:0"};
    // Match Ada's 2 cm voxels, 8-voxel blocks and 10 cm TSDF truncation.
    o3g::VoxelBlockGrid volume{{"tsdf", "weight"}, {o3c::Float32, o3c::Float32}, {{1}, {1}}, 0.02f, 8, 10000, device};
    o3c::Tensor         intrinsic;
    int                 width        = 0;
    int                 height       = 0;
    float               depth_scale  = 1000.0f;
    float               depth_offset = 0.0f;
    block_set           dirty;
    o3c::Tensor         host_positions, host_indices, host_blocks;

    explicit implementation(const std::string& directory) {
        std::ifstream calibration(directory + "/calibration.txt");
        int           rgb_width, rgb_height;
        double        rgb_fx, rgb_fy, rgb_cx, rgb_cy, fx, fy, cx, cy;
        calibration >> rgb_width >> rgb_height >> rgb_fx >> rgb_fy >> rgb_cx >> rgb_cy;
        calibration >> width >> height >> fx >> fy >> cx >> cy;
        double extrinsics[12];
        for (auto& value : extrinsics)
            calibration >> value;
        std::string model;
        float       scale;
        calibration >> model >> scale >> depth_offset;
        if (!calibration || model != "affine" || width <= 0 || height <= 0 || !(fx > 0) || !(fy > 0) || !std::isfinite(fx) ||
            !std::isfinite(fy) || !std::isfinite(cx) || !std::isfinite(cy) || !(scale > 0) || !std::isfinite(scale) ||
            !std::isfinite(depth_offset)) {
            throw std::runtime_error("ada.open3d requires valid affine depth calibration in " + directory + "/calibration.txt");
        }
        depth_scale = 1.0f / scale;
        intrinsic   = o3c::Tensor(std::vector<double>{fx, 0, cx, 0, fy, cy, 0, 0, 1}, {3, 3});
    }
};

reconstructor::reconstructor(const std::string& directory) {
    if (!o3c::cuda::IsAvailable())
        throw std::runtime_error("ada.open3d requires a CUDA-enabled Open3D build and an available NVIDIA GPU");
    state_ = std::make_unique<implementation>(directory);
}

reconstructor::~reconstructor() = default;

void reconstructor::integrate(const uint16_t* input_depth, int width, int height, size_t stride,
                              const std::array<float, 16>& pose) {
    auto& state = *state_;
    if (width != state.width || height != state.height)
        throw std::runtime_error("ada.open3d received depth inconsistent with the calibration");
    std::vector<double> extrinsic(16, 0.0);
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c)
            extrinsic[4 * r + c] = pose[4 * c + r];
        extrinsic[4 * r + 3] = -(pose[r] * pose[3] + pose[4 + r] * pose[7] + pose[8 + r] * pose[11]);
    }
    extrinsic[15] = 1;
    o3c::Tensor transform(std::move(extrinsic), {4, 4});
    o3c::Tensor depth_cpu({state.height, state.width, 1}, o3c::Float32);
    float*      pixels       = depth_cpu.GetDataPtr<float>();
    bool        valid_sample = false;
    for (int y = 0; y < state.height; ++y) {
        const auto* input = reinterpret_cast<const uint16_t*>(reinterpret_cast<const uint8_t*>(input_depth) + y * stride);
        for (int x = 0; x < state.width; ++x) {
            const float d               = input[x] / state.depth_scale + state.depth_offset;
            const bool  valid           = input[x] != 0 && d >= 0.2f && d < 4.0f;
            pixels[y * state.width + x] = valid ? d : 0.0f;
            // Open3D's block activation samples the depth image at stride 4.
            valid_sample |= valid && x % 4 == 0 && y % 4 == 0;
        }
    }
    if (!valid_sample)
        return;
    o3g::Image depth(depth_cpu.To(state.device));
    auto       blocks = state.volume.GetUniqueBlockCoordinates(depth, state.intrinsic, transform, 1.0f, 4.0f, 5.0f);
    state.volume.Integrate(blocks, depth, state.intrinsic, transform, 1.0f, 4.0f, 5.0f);
    const auto  host_blocks = blocks.To(o3c::Device("CPU:0"));
    const auto* keys        = host_blocks.GetDataPtr<int>();
    for (int64_t i = 0; i < host_blocks.GetLength(); ++i) {
        // Negative neighbors can own cubes sampling a changed boundary voxel.
        // Retain coordinate keys: hash-buffer indices can move during growth.
        for (int dz = -1; dz <= 0; ++dz)
            for (int dy = -1; dy <= 0; ++dy)
                for (int dx = -1; dx <= 0; ++dx)
                    state.dirty.emplace(keys[3 * i] + dx, keys[3 * i + 1] + dy, keys[3 * i + 2] + dz);
    }
}

mesh_view reconstructor::extract() {
    auto&            state = *state_;
    std::vector<int> coordinates;
    coordinates.reserve(state.dirty.size() * 3);
    for (const auto& [x, y, z] : state.dirty)
        coordinates.insert(coordinates.end(), {x, y, z});
    o3c::Tensor selected(coordinates, {static_cast<int64_t>(state.dirty.size()), 3}, o3c::Int32);
    auto        mesh     = state.volume.ExtractTriangleMeshForBlocks(selected.To(state.device), 3.0f);
    state.host_positions = mesh.GetVertexPositions().To(o3c::Device("CPU:0"));
    state.host_indices   = mesh.GetTriangleIndices().To(o3c::Device("CPU:0"));
    state.host_blocks    = mesh.GetTriangleAttr("block_coords").To(o3c::Device("CPU:0"));
    const auto faces     = state.host_indices.GetLength();
    if (faces > std::numeric_limits<unsigned>::max() / 3)
        throw std::overflow_error("Open3D mesh exceeds Ada chunk count capacity");
    return {state.host_positions.GetDataPtr<float>(), state.host_indices.GetDataPtr<int>(), state.host_blocks.GetDataPtr<int>(),
            static_cast<unsigned>(faces)};
}

block_set reconstructor::take_updated_blocks() {
    return std::exchange(state_->dirty, {});
}
} // namespace ILLIXR::ada_open3d
