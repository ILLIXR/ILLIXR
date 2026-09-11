#pragma once

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

// 9/2 used to store nullified ranges, first two are start and end indices of the nullified range, followed by the face vector
// content using Nullified_Ranges = std::tuple<int, int, std::vector<Eigen::Vector3i>>;
using Nullified_Ranges = std::tuple<int, int, std::vector<int>>;

class spatial_hash {
public:
    using SceneUpdateMap = std::unordered_map<unsigned, std::vector<NewVB>>;

    // Immutable chunks can contribute multiple fragments to the same block.
    // Keep their buffers in arrival order without concatenating vertex arrays.
    struct VertexFragments {
        std::vector<const std::vector<Eigen::Vector3d>*> buffers;
        size_t                                           vertex_count = 0;

        void append(const std::vector<Eigen::Vector3d>& vertices) {
            buffers.push_back(&vertices);
            vertex_count += vertices.size();
        }

        size_t size() const {
            return vertex_count;
        }

        template<typename OutputIt>
        void copy_to(OutputIt destination) const {
            for (const auto* buffer : buffers)
                destination = std::copy(buffer->begin(), buffer->end(), destination);
        }

        void append_to(std::vector<Eigen::Vector3d>& destination) const {
            for (const auto* buffer : buffers)
                destination.insert(destination.end(), buffer->begin(), buffer->end());
        }
    };

    using PendingVB = std::tuple<VoxelBlockIndex, VertexFragments>;

    spatial_hash();

    [[maybe_unused]] void clean_mesh_vb_redesign_with_list(const std::set<std::tuple<int, int, int>>& vb_lists);

    void deleted_ranges_processing();

    // Retain shared ownership until append_mesh_match_and_insert has consumed
    // all fragments. The caller may release its own chunk references sooner.
    void append_mesh_allocate(std::shared_ptr<const SceneUpdateMap> inputSceneUpdateMap);

    unsigned        append_mesh_match_and_insert(bool merge);
    static unsigned hash_vb(const VoxelBlockIndex& Index);

    void restore_deleted_faces();

    // utility function
    [[maybe_unused]] void print_mesh_as_obj(unsigned frame_id, unsigned type, const std::string& tr);

    // 7/22
    std::unordered_map<unsigned, std::vector<Vector_range>> map_VB_to_range_;
    std::unordered_map<unsigned, std::vector<PendingVB>>    allocate_new_VB_;
    // changed to int since map_VB_to_range_ is int, int (needs to deal with deleted range)
    // however deleted range should not be negative
    std::vector<std::pair<int, int>> deleted_ranges_;

    // this is the internal data structure
    std::vector<Eigen::Vector3d> vertices_;
    // std::vector<Eigen::Vector3d> colors;
    // std::vector<Eigen::Vector3i> faces;
    std::vector<int> faces_;

    // 92 fix mesh nullification by storing the face sub-vectors of deleted_faces
    std::vector<Nullified_Ranges> nullified_ranges;

    // for multi scene merging only should be cleared during normal scenario
    std::set<std::tuple<int, int, int, int>> older_scene_vb_list;

    unsigned delete_counter_;
    unsigned VB_skipped_;

    // 528 this is essentially creating a really large face so we can quickly generate the face vector
    std::vector<int> faces_base_;

private:
    std::vector<std::shared_ptr<const SceneUpdateMap>> input_owners_;
};

[[maybe_unused]] void track_time(const std::string& message, const std::function<void()>& func);
} // namespace ILLIXR
