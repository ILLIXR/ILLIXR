#include "plugin.hpp"

#include "draco_mesh_builder.hpp"
#include "illixr/phonebook.hpp"
#include "reconstruction.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <exception>
#include <filesystem>
#include <limits>
#include <omp.h>
#include <set>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <tuple>

using namespace ILLIXR;
using namespace ILLIXR::data_format;
using clock_type = std::chrono::steady_clock;

static double elapsed_ms(clock_type::time_point start) {
    return std::chrono::duration<double, std::milli>(clock_type::now() - start).count();
}

open3d_reconstruction::open3d_reconstruction(const std::string& name, phonebook* pb)
    : plugin{name, pb}
    , switchboard_{pb->lookup_impl<switchboard>()}
    , mesh_writer_{switchboard_->get_writer<mesh_type>("requested_scene")}
    , block_writer_{switchboard_->get_writer<vb_type>("unique_VB_list")}
    , interval_{static_cast<unsigned>(switchboard_->get_env_ulong("FPS", 15))}
    , workers_{static_cast<unsigned>(switchboard_->get_env_ulong("MESH_COMPRESS_PARALLELISM", 8))} {
    if (interval_ == 0 || workers_ == 0 || workers_ > static_cast<unsigned>(std::numeric_limits<int>::max()))
        throw std::invalid_argument("ada.open3d requires positive FPS and MESH_COMPRESS_PARALLELISM");
    if (workers_ != switchboard_->get_env_ulong("PARTIAL_MESH_COUNT", 8))
        throw std::invalid_argument("Ada MESH_COMPRESS_PARALLELISM must equal PARTIAL_MESH_COUNT");
    const auto directory = switchboard_->get_env("ILLIXR_DATA");
    if (directory.empty())
        throw std::runtime_error("ILLIXR_DATA not set");
    state_ = std::make_unique<ada_open3d::reconstructor>(directory);
    std::filesystem::create_directories("recorded_data");
    latency_.open("recorded_data/sr_latency.csv");
    if (!latency_)
        throw std::runtime_error("Could not open Open3D reconstruction timing log");
    omp_set_dynamic(0);
    switchboard_->schedule<scene_recon_type>(id_, "ScanNet_Data",
                                             [this](switchboard::ptr<const scene_recon_type> datum, std::size_t) {
                                                 process_frame(datum);
                                             });
    spdlog::get("illixr")->info("Ada reconstruction: Open3D CUDA, voxel=0.02 m, block=8, truncation=0.1 m, workers={}",
                                workers_);
}

open3d_reconstruction::~open3d_reconstruction() = default;

void open3d_reconstruction::process_frame(const switchboard::ptr<const scene_recon_type>& datum) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto                  start = clock_type::now();
    if (datum->depth.empty()) {
        ++frame_;
        return;
    }
    if (datum->depth.type() != CV_16UC1)
        throw std::runtime_error("ada.open3d requires UInt16 depth images");
    const auto            rotation = datum->pose.orientation.toRotationMatrix();
    std::array<float, 16> pose{};
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c)
            pose[4 * r + c] = rotation(r, c);
        pose[4 * r + 3] = datum->pose.position[r];
    }
    pose[15] = 1;
    state_->integrate(datum->depth.ptr<uint16_t>(), datum->depth.cols, datum->depth.rows, datum->depth.step, pose);
    latency_ << "fusion " << frame_ << ' ' << elapsed_ms(start) << '\n';
    if (frame_ > 0 && frame_ % interval_ == 0)
        extract(frame_ / interval_ - 1);
    ++frame_;
}

void open3d_reconstruction::extract(unsigned scene_id) {
    const auto start = clock_type::now();
    const auto epoch =
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    const auto mesh  = state_->extract();
    const auto faces = mesh.faces;
    latency_ << "extract " << scene_id << ' ' << elapsed_ms(start) << ' ' << faces << '\n';
    latency_ << "extract_start_ns " << scene_id << ' ' << epoch << '\n';
    // Includes owners that now emit no triangles, so stale geometry is erased.
    block_writer_.put(block_writer_.allocate<vb_type>(vb_type{state_->take_updated_blocks(), scene_id}));
    const auto         build_start = clock_type::now();
    const unsigned     per_worker  = (faces + workers_ - 1) / workers_;
    std::exception_ptr error;
    std::mutex         error_mutex;
#pragma omp parallel for num_threads(workers_)
    for (unsigned chunk = 0; chunk < workers_; ++chunk) {
        try {
            const unsigned first = std::min(chunk * per_worker, faces);
            const unsigned count = std::min(per_worker, faces - first);
            auto           draco = make_open3d_draco_mesh(mesh.positions, mesh.indices, mesh.blocks, first, count);
            if (!draco)
                throw std::runtime_error("Open3D chunk construction failed");
            mesh_writer_.put(mesh_writer_.allocate<mesh_type>(
                mesh_type{chunk, std::move(draco), scene_id, chunk, workers_, count, count * 3, false, true}));
        } catch (...) {
            std::lock_guard<std::mutex> lock(error_mutex);
            if (!error)
                error = std::current_exception();
        }
    }
    if (error)
        std::rethrow_exception(error);
    latency_ << "gen " << scene_id << ' ' << elapsed_ms(build_start) << '\n';
    latency_.flush();
    spdlog::get("illixr")->info("Open3D scene {}: {} faces across {} chunks", scene_id, faces, workers_);
}

PLUGIN_MAIN(open3d_reconstruction)
