#pragma once

#include "free_range_index.hpp"
#include "illixr/data_format/scene_update.hpp"

#include <algorithm>
#include <eigen3/Eigen/Dense>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace ILLIXR {
using VoxelBlockIndex = std::tuple<int, int, int>;
// index of -1 mean it is cleaned, this should be the range of face
using Vector_range = std::tuple<VoxelBlockIndex, int, int>;
// vertices			colors
using NewVB = std::tuple<VoxelBlockIndex, std::vector<Eigen::Vector3d>, std::vector<Eigen::Vector3d>>;

class spatial_hash {
public:
    using SceneUpdateMap    = std::unordered_map<unsigned, std::vector<NewVB>>;
    using SceneUpdateRanges = data_format::scene_update_data::RangeMap;

    // Immutable chunks can contribute multiple fragments to the same block.
    // Keep their buffers in arrival order without concatenating vertex arrays.
    struct VertexFragments {
        struct Fragment {
            const data_format::scene_vertex* data;
            const Eigen::Vector3d*           legacy_data;
            size_t                           count;
        };

        std::vector<Fragment> buffers;
        size_t                vertex_count = 0;

        void append(const std::vector<Eigen::Vector3d>& vertices) {
            buffers.push_back({nullptr, vertices.data(), vertices.size()});
            vertex_count += vertices.size();
        }

        void append(const data_format::scene_vertex_range& vertices) {
            buffers.push_back({vertices.data(), nullptr, vertices.size()});
            vertex_count += vertices.size();
        }

        size_t size() const {
            return vertex_count;
        }

        template<typename OutputIt>
        void copy_to(OutputIt destination) const {
            for (const auto& buffer : buffers) {
                if (buffer.data) {
                    destination = std::copy(buffer.data, buffer.data + buffer.count, destination);
                } else {
                    // Older Draco events may still carry double vectors.
                    for (size_t i = 0; i < buffer.count; ++i)
                        *destination++ = buffer.legacy_data[i].cast<float>();
                }
            }
        }

        void append_to(std::vector<data_format::scene_vertex>& destination) const {
            for (const auto& buffer : buffers) {
                if (buffer.data) {
                    destination.insert(destination.end(), buffer.data, buffer.data + buffer.count);
                } else {
                    for (size_t i = 0; i < buffer.count; ++i)
                        destination.emplace_back(buffer.legacy_data[i].cast<float>());
                }
            }
        }
    };

    using PendingVB = std::tuple<VoxelBlockIndex, VertexFragments>;

    spatial_hash();

    [[maybe_unused]] void clean_mesh_vb_redesign_with_list(const std::set<std::tuple<int, int, int>>& vb_lists);

    void deleted_ranges_processing();

    // Retain shared ownership until append_mesh_match_and_insert has consumed
    // all fragments. The caller may release its own chunk references sooner.
    void append_mesh_allocate(std::shared_ptr<const SceneUpdateMap> inputSceneUpdateMap);
    void append_mesh_allocate(std::shared_ptr<const SceneUpdateRanges> inputSceneUpdateMap);

    unsigned        append_mesh_match_and_insert(bool merge);
    static unsigned hash_vb(const VoxelBlockIndex& Index);

    // utility function
    [[maybe_unused]] void print_mesh_as_obj(unsigned frame_id, unsigned type, const std::string& tr);

    // 7/22
    std::unordered_map<unsigned, std::vector<Vector_range>> map_VB_to_range_;
    std::unordered_map<unsigned, std::vector<PendingVB>>    allocate_new_VB_;
    // changed to int since map_VB_to_range_ is int, int (needs to deal with deleted range)
    // however deleted range should not be negative
    std::vector<std::pair<int, int>> deleted_ranges_;

    // this is the internal data structure
    std::vector<data_format::scene_vertex> vertices_;
    // std::vector<Eigen::Vector3d> colors;
    // std::vector<Eigen::Vector3i> faces;
    std::vector<int> faces_;

    // for multi scene merging only should be cleared during normal scenario
    std::set<std::tuple<int, int, int, int>> older_scene_vb_list;

    unsigned delete_counter_;
    unsigned VB_skipped_;

    // 528 this is essentially creating a really large face so we can quickly generate the face vector
    std::vector<int> faces_base_;

private:
    struct PendingPlacement {
        const PendingVB* block;
        unsigned         hash;
        unsigned         faces;
    };

    template<typename Map>
    void append_mesh_allocate_impl(std::shared_ptr<const Map> inputSceneUpdateMap);

    std::vector<std::shared_ptr<const void>> input_owners_;

    // Pending blocks are immutable during placement, so their addresses remain
    // valid while this list is sorted. Retain only scratch capacity between updates.
    std::vector<PendingPlacement>    pending_placements_;
    std::vector<std::pair<int, int>> remaining_deleted_ranges_;

    free_range_index free_ranges_;

    // Sorted, disjoint inclusive face ranges that were unused after the last update.
    // Keep only their boundaries; live indices can be recovered from faces_base_.
    std::vector<std::pair<int, int>> nullified_ranges_;
};

[[maybe_unused]] void track_time(const std::string& message, const std::function<void()>& func);
} // namespace ILLIXR
