#pragma once

#include <cstddef>
#include <eigen3/Eigen/Dense>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace ILLIXR::data_format {

// A block's vertices occupy a range within one decoded chunk. The owner of the
// chunk keeps its storage alive until all readers have finished with the range.
struct scene_vertex_range {
    const std::vector<Eigen::Vector3d>* storage = nullptr;
    size_t                              offset  = 0;
    size_t                              count   = 0;

    size_t size() const {
        return count;
    }

    bool empty() const {
        return count == 0;
    }

    const Eigen::Vector3d* data() const {
        return count ? storage->data() + offset : nullptr;
    }

    const Eigen::Vector3d* begin() const {
        return data();
    }

    const Eigen::Vector3d* end() const {
        return count ? data() + count : nullptr;
    }
};

struct scene_update_data {
    using Block    = std::tuple<std::tuple<int, int, int>, scene_vertex_range, scene_vertex_range>;
    using RangeMap = std::unordered_map<unsigned, std::vector<Block>>;

    std::vector<Eigen::Vector3d> vertices;
    RangeMap                     block_ranges;

    scene_update_data() = default;
    // Ranges refer to the vector object, whose address must remain stable.
    scene_update_data(const scene_update_data&)            = delete;
    scene_update_data& operator=(const scene_update_data&) = delete;
    scene_update_data(scene_update_data&&)                 = delete;
    scene_update_data& operator=(scene_update_data&&)      = delete;
};

} // namespace ILLIXR::data_format
