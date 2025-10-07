#pragma once

#include <eigen3/Eigen/Dense>
#include <set>
#include <tuple>
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
    spatial_hash();

    [[maybe_unused]] void clean_mesh_vb_redesign_with_list(const std::set<std::tuple<int, int, int>>& vb_lists);

    void deleted_ranges_processing();

    // 91 changed to accept scene update mappings instead
    void append_mesh_allocate(const std::unordered_map<unsigned, std::vector<NewVB>>& inputSceneUpdateMap);

    unsigned        append_mesh_match_and_insert(bool merge);
    static unsigned hash_vb(const VoxelBlockIndex& Index);

    void restore_deleted_faces();

    // utility function
    [[maybe_unused]] void print_mesh_as_obj(unsigned frame_id, unsigned type, const std::string& tr);

    // 7/22
    std::unordered_map<unsigned, std::vector<Vector_range>> map_VB_to_range_;
    // this is tracking locally created vB sub-vectors?
    std::unordered_map<unsigned, std::vector<NewVB>> allocate_new_VB_;
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
};

[[maybe_unused]] void track_time(const std::string& message, const std::function<void()>& func);
} // namespace ILLIXR
