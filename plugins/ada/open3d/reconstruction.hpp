#pragma once

// Keep Open3D's public headers (and its fmt version) out of ILLIXR translation
// units. This boundary carries only standard C++ types and borrowed mesh data.
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <tuple>

namespace ILLIXR::ada_open3d {
using block_set = std::set<std::tuple<int, int, int>>;

struct mesh_view {
    const float* positions;
    const int*   indices;
    const int*   blocks;
    unsigned     faces;
};

class reconstructor {
public:
    explicit reconstructor(const std::string& data_directory);
    ~reconstructor();
    void integrate(const uint16_t* depth, int width, int height, size_t row_stride,
                   const std::array<float, 16>& camera_to_world);
    // Views remain valid until the next extraction or destruction. Ada finishes
    // constructing owned Draco chunks before it integrates the next frame.
    mesh_view extract();
    block_set take_updated_blocks();

private:
    struct implementation;
    std::unique_ptr<implementation> state_;
};
} // namespace ILLIXR::ada_open3d
