#include "spatial_hash.hpp"

#include <fstream>
#include <spdlog/spdlog.h>

using namespace ILLIXR;

spatial_hash::spatial_hash() {
    // map_VB_to_range_.reserve(1000000);
    map_VB_to_range_.reserve(25600);
    deleted_ranges_.reserve(10000);

    vertices_.reserve(5000000);
    // colors.reserve(5000000);
    faces_.reserve(1000000);

    // 92 fix mesh nullification by storing the face sub-vectors of deleted_faces
    nullified_ranges.reserve(100000);
    faces_base_.reserve(30000000);

    delete_counter_ = 0;
    VB_skipped_     = 0;

    for (unsigned i = 0; i < 10000000; i++) {
        unsigned f_base = i * 3;
        // since we are using std::vector<int> instead
        faces_base_.emplace_back(f_base);
        faces_base_.emplace_back(f_base + 1);
        faces_base_.emplace_back(f_base + 2);
    }
}

unsigned spatial_hash::hash_vb(const VoxelBlockIndex& Index) {
    int x, y, z;
    std::tie(x, y, z) = Index;
    auto hash         = (x * 73856093) ^ (y * 19349669) ^ (z * 83492791);
    return std::abs(hash) % 25600;
}

[[maybe_unused]] void track_time(const std::string& message, const std::function<void()>& func) {
    auto start = std::chrono::high_resolution_clock::now();
    func();
    auto                                      end     = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed = end - start;
    spdlog::get("illixr")->debug(message + ": {} ms", elapsed.count());
}

void spatial_hash::restore_deleted_faces() {
    for (auto& nullified_range : nullified_ranges) {
        int start_index = std::get<0>(nullified_range);
        // int               end_index         = std::get<1>(nullified_range);
        std::vector<int>& saved_face_vector = std::get<2>(nullified_range);

        std::move(saved_face_vector.begin(), saved_face_vector.end(), faces_.begin() + start_index * 3);
    }
    nullified_ranges.clear();
}

[[maybe_unused]] void spatial_hash::clean_mesh_vb_redesign_with_list(const std::set<std::tuple<int, int, int>>& vb_lists) {
    delete_counter_ = 0;
    VB_skipped_     = 0;

#ifndef NDEBUG
    unsigned vb_count = 0;
    for (auto& map_it : map_VB_to_range_) {
        for ([[maybe_unused]] auto& vector_it : map_it.second) {
            vb_count++;
        }
    }
    spdlog::get("illixr")->debug("vb_count before cleaning {}", vb_count);
#endif

    // for each vb that has existing entry, mark its range as -1 to -1 since a valid range should have start at least greater
    // than 0
    for (const auto& each_vb : vb_lists) {
        VoxelBlockIndex vb_index{std::get<0>(each_vb), std::get<1>(each_vb), std::get<2>(each_vb)};
        unsigned        hash_idx = hash_vb(vb_index);
        auto            it       = map_VB_to_range_.find(hash_idx);
        if (it != map_VB_to_range_.end()) {
            bool found       = false;
            bool already_set = false;
            for (auto& vb_entry : it->second) {
                VoxelBlockIndex cur_vb = std::get<0>(vb_entry);
                if (vb_index == cur_vb) {
                    // VB already existed
                    auto deleted_range = std::make_pair(std::get<1>(vb_entry), std::get<2>(vb_entry));
                    if (std::get<0>(deleted_range) < 0 || std::get<1>(deleted_range) < 0) {
                        // this is corner case where a VB usually with one face is extracted but when compressing the face is
                        // removed thus the range did not get updated in the previous version we simply skip;
#ifndef NDEBUG
                        spdlog::get("illixr")->debug("deleted range is already negative, vb %d, %d, %d, range %d, %d",
                                                     std::get<0>(vb_index), std::get<1>(vb_index), std::get<2>(vb_index),
                                                     deleted_range.first, deleted_range.second);
                        already_set = true;
#endif
                        break;
                    }
                    std::get<1>(vb_entry) = -1;
                    std::get<2>(vb_entry) = -1;
                    // deleted range contains the ranges belonging to the deleted vbs
                    deleted_ranges_.emplace_back(deleted_range);
#ifndef NDEBUG
                    spdlog::get("illixr")->debug("deleted VB: %d, %d, %d, range %d, %d", std::get<0>(vb_index),
                                                 std::get<1>(vb_index), std::get<2>(vb_index), deleted_range.first,
                                                 deleted_range.second);
#endif
                    found = true;
                    delete_counter_++;
                    break;
                }
            }
            // hash collision exists but no actual VB found, treat as new VB
            if (!found && !already_set) {
#ifndef NDEBUG
                spdlog::get("illixr")->debug("new VB: %d, %d, %d, hash: %u", std::get<0>(vb_index), std::get<1>(vb_index),
                                             std::get<2>(vb_index), hash_idx);
#endif
                VB_skipped_++;
            }
        }
        // if this is a new VB
        else {
            // we also don't want new vbs to be checked as well
#ifndef NDEBUG
            spdlog::get("illixr")->debug("new VB: %d, %d, %d, hash: %u", std::get<0>(vb_index), std::get<1>(vb_index),
                                         std::get<2>(vb_index), hash_idx);
#endif
            VB_skipped_++;
        }
    }
#ifndef NDEBUG
    spdlog::get("illixr")->debug("existing VB cleared %u, new VB inserted %u", delete_counter_, VB_skipped_);
#endif
}

void spatial_hash::deleted_ranges_processing() {
#ifndef NDEBUG
    auto start = std::chrono::high_resolution_clock::now();
    spdlog::get("illixr")->debug("deleted range %zu", deleted_ranges_.size());
    unsigned counter = 0;
    for (const auto& range_it : deleted_ranges_) {
        spdlog::get("illixr")->debug("pre-merging deleted range %u, range: %d, %d", counter, range_it.first, range_it.second);
        counter++;
    }
#endif
    if (!deleted_ranges_.empty()) {
        // Sort the ranges in ascending order based on the start index (Section 4.3 stage 1)
        std::sort(deleted_ranges_.begin(), deleted_ranges_.end());
        // Merge overlapping ranges directly into deleted_ranges_
        size_t j = 0;
        for (size_t i = 1; i < deleted_ranges_.size(); ++i) {
            if (deleted_ranges_[j].second == deleted_ranges_[i].first - 1) {
                deleted_ranges_[j].second = std::max(deleted_ranges_[j].second, deleted_ranges_[i].second);
            } else if (deleted_ranges_[j].second > deleted_ranges_[i].first - 1) {
                spdlog::get("illixr")->error("overlap should never happen %d, %d", deleted_ranges_[j].second,
                                             deleted_ranges_[j].first);
            } else {
                deleted_ranges_[++j] = deleted_ranges_[i];
            }
        }
        deleted_ranges_.resize(j + 1);
    }

#ifndef NDEBUG
    auto end      = std::chrono::high_resolution_clock::now();
    auto duration = static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count()) / 1000.0;
    spdlog::get("illixr")->debug("Merge deleted ranges: %.3f ms", duration);
    spdlog::get("illixr")->debug("deleted range %zu", deleted_ranges_.size());
    counter = 0;
    for (const auto& range_it : deleted_ranges_) {
        spdlog::get("illixr")->debug("post-merging deleted range %u, range: %d, %d", counter, range_it.first, range_it.second);
        counter++;
    }
#endif
}

void spatial_hash::append_mesh_allocate(const std::unordered_map<unsigned, std::vector<NewVB>>& inputSceneUpdateMap) {
    // Just append to the existing allocate_new_VB_
    for (const auto& pair : inputSceneUpdateMap) {
        unsigned    hash_idx = pair.first;
        const auto& new_vbs  = pair.second;

        // Check if the key exists in the target map
        auto it = allocate_new_VB_.find(hash_idx);
        if (it != allocate_new_VB_.end()) {
            // If key exists, see if it can find matching VB
            for (const auto& new_vb : new_vbs) {
                VoxelBlockIndex cur_vb       = std::get<0>(new_vb);
                auto&           cur_vertices = std::get<1>(new_vb);

                // For every new vb, check the existing entries
                bool found_vb = false;
                for (auto& vb_entry : it->second) {
                    VoxelBlockIndex stored_vb = std::get<0>(vb_entry);
                    if (cur_vb == stored_vb) {
                        found_vb            = true;
                        auto& vertices_ ? ? = std::get<1>(vb_entry);

                        // Efficiently move-append the new vertices and colors to the existing ones
                        vertices_.insert(vertices_.end(), std::make_move_iterator(cur_vertices.begin()),
                                         std::make_move_iterator(cur_vertices.end()));
                        break;
                    }
                }
                if (!found_vb) {
                    // If not found, move the new VB into the existing map
                    it->second.push_back(new_vb);
                }
            }
        } else {
            // If key does not exist, move the entire vector to avoid copying
            allocate_new_VB_[hash_idx] = new_vbs;
        }
    }
}

// pyh merge is for a feature we never used in publication, so ignore
unsigned spatial_hash::append_mesh_match_and_insert(bool merge) {
    (void) merge;
    // Get number of faces for each VB
    std::vector<std::pair<VoxelBlockIndex, unsigned>> sizes;
    // auto                                              original_deleted_ranges = deleted_ranges_;

    // for all new incoming VB get the face size
    for (const auto& entry : allocate_new_VB_) {
        for (const auto& vb_entry : entry.second) {
            const VoxelBlockIndex&              vb_index     = std::get<0>(vb_entry);
            const std::vector<Eigen::Vector3d>& first_vector = std::get<1>(vb_entry);
            // it is inserting vertices & colors, so # of faces = size()/3
            sizes.emplace_back(vb_index, first_vector.size() / 3);
        }
    }
    // sort vb in descinding order based on number of faces (see S4.3 stage 2 last sentence)
    std::sort(sizes.begin(), sizes.end(),
              [](const std::pair<VoxelBlockIndex, unsigned>& a, const std::pair<VoxelBlockIndex, unsigned>& b) {
                  return a.second > b.second;
              });

#ifndef NDEBUG
    for (const auto& entry : allocate_new_VB_) {
        for (const auto& vb_entry : entry.second) {
            const VoxelBlockIndex&              vb_index     = std::get<0>(vb_entry);
            const std::vector<Eigen::Vector3d>& first_vector = std::get<1>(vb_entry);
            spdlog::get("illixr")->debug("VB: %d, %d, %d, with %lu faces", std::get<0>(vb_index), std::get<1>(vb_index),
                                         std::get<2>(vb_index), first_vector.size());
        }
    }
#endif

    // need to track how many faces added to the end
    unsigned new_faces = 0;

    // For each VB try to pack it into existing deleted range as best as possible, starting from the largest one
    for (const auto& vb_size : sizes) {
        // cond 1: check if it is packed
        bool packed = false;
#ifndef NDEBUG
        spdlog::get("illixr")->debug("packing VB: %d, %d, %d with %d faces", std::get<0>(vb_size.first),
                                     std::get<1>(vb_size.first), std::get<2>(vb_size.first), vb_size.second);
#endif
        for (auto& range : deleted_ranges_) {
            if (range.first > range.second) {
                continue;
            }
            int range_size = range.second - range.first + 1;
#ifndef NDEBUG
            spdlog::get("illixr")->debug("attempting to fit VB: %d, %d, %d with %d faces, into range %d, of %d to %d",
                                         std::get<0>(vb_size.first), std::get<1>(vb_size.first), std::get<2>(vb_size.first),
                                         vb_size.second, range_size, range.first, range.second);
#endif
            if (static_cast<int>(vb_size.second) <= range_size) {
                // available deleted range is larger (can fit) incoming voxel block
                // Pack the VB into this range
                auto     packing_vb = vb_size.first;
                unsigned hash_idx   = hash_vb(packing_vb);
                auto     alloc_it   = allocate_new_VB_.find(hash_idx);
                if (alloc_it == allocate_new_VB_.end()) {
                    spdlog::get("illixr")->error(
                        "Should not happen: Cannot find packing vb in allocate_new_VB_, should allocated in the first pass");
                } else {
                    // cond 2: check vb is found in allocate_new_VB_
                    bool vb_found = false;
                    // each entry, vb_index, vertices vector, colors vector
                    for (auto& vb_entry : alloc_it->second) {
                        VoxelBlockIndex vb_index = std::get<0>(vb_entry);
                        if (vb_index == packing_vb) {
                            vb_found         = true;
                            auto vb_vertices = std::get<1>(vb_entry);

                            std::move(vb_vertices.begin(), vb_vertices.end(), vertices_.begin() + range.first * 3);

                            std::copy(faces_base_.begin() + range.first * 3, faces_base_.begin() + (range.second + 1) * 3,
                                      faces_.begin() + range.first * 3);

                            auto map_it = map_VB_to_range_.find(hash_idx);
                            if (map_it == map_VB_to_range_.end()) {
                                // cannot find the VB-> new VB
#ifndef NDEBUG
                                spdlog::get("illixr")->debug(
                                    "allocating new VB: %d, %d, %d in map_VB_to_range_ with range %d, %d",
                                    std::get<0>(vb_size.first), std::get<1>(vb_size.first), std::get<2>(vb_size.first),
                                    range.first, range.first + vb_size.second - 1);
#endif
                                if (range.first < 0) {
                                    spdlog::get("illixr")->error("Should not happen #1 happened here, vb: %d, %d, %d",
                                                                 std::get<0>(vb_size.first), std::get<1>(vb_size.first),
                                                                 std::get<2>(vb_size.first));
                                }
                                auto new_range = std::make_tuple(packing_vb, range.first, range.first + vb_size.second - 1);
                                std::vector<Vector_range> hash_entry(1, new_range);
                                map_VB_to_range_[hash_idx] = std::move(hash_entry);

                                // update the deleted range
                                range.first += static_cast<int>(vb_size.second);
                            } else {
                                bool mapping_found = false;
                                for (auto& map_vb_entry : map_it->second) {
                                    VoxelBlockIndex cur_map_vb = std::get<0>(map_vb_entry);
                                    if (cur_map_vb == packing_vb) {
#ifndef NDEBUG
                                        spdlog::get("illixr")->debug(
                                            "update existing VB: %d, %d, %d with old range %d, %d in map_VB_to_range_ with "
                                            "range %d, %d",
                                            std::get<0>(vb_size.first), std::get<1>(vb_size.first), std::get<2>(vb_size.first),
                                            std::get<1>(map_vb_entry), std::get<2>(map_vb_entry), range.first,
                                            range.first + vb_size.second - 1);
#endif
                                        // check to see if they are both -1 (they should be since cleaning will set them to -1)
                                        if (std::get<1>(map_vb_entry) != -1 || std::get<2>(map_vb_entry) != -1) {
                                            spdlog::get("illixr")->error(
                                                "Should not happen, the existing VB does not have range of -1 to -1, vb %d, "
                                                "%d, %d, range %d, %d",
                                                std::get<0>(vb_size.first), std::get<1>(vb_size.first),
                                                std::get<2>(vb_size.first), std::get<1>(map_vb_entry),
                                                std::get<2>(map_vb_entry));
                                        }

                                        if (range.first < 0) {
                                            spdlog::get("illixr")->error("Should not happen #2 happened here");
                                        }

                                        // update the new range
                                        std::get<1>(map_vb_entry) = range.first;
                                        std::get<2>(map_vb_entry) = static_cast<int>(range.first + vb_size.second - 1);

                                        // update the deleted range
                                        range.first += static_cast<int>(vb_size.second);

                                        mapping_found = true;
                                        break;
                                    }
                                }
                                if (!mapping_found) {
                                    if (range.first < 0) {
                                        spdlog::get("illixr")->error("Should not happen #3 happened here");
                                    }
                                    // hash collision case
                                    auto new_range = std::make_tuple(packing_vb, range.first, range.first + vb_size.second - 1);
                                    map_VB_to_range_[hash_idx].emplace_back(std::move(new_range));
#ifndef NDEBUG
                                    spdlog::get("illixr")->debug(
                                        "Type 2: allocating new VB: %d, %d, %d in map_VB_to_range_ with range %d, %d",
                                        std::get<0>(vb_size.first), std::get<1>(vb_size.first), std::get<2>(vb_size.first),
                                        range.first, range.first + vb_size.second - 1);
                                    // Print all VBs sharing this hash index
                                    spdlog::get("illixr")->debug("Hash index %u shared by the following Voxel Blocks:",
                                                                 hash_idx);
                                    for (auto& map_vb_entry : map_VB_to_range_[hash_idx]) {
                                        VoxelBlockIndex test_vb = std::get<0>(map_vb_entry);
                                        spdlog::get("illixr")->debug(
                                            "  VB: %d, %d, %d with range [%d, %d]", std::get<0>(test_vb), std::get<1>(test_vb),
                                            std::get<2>(test_vb), std::get<1>(map_vb_entry), std::get<2>(map_vb_entry));
                                    }
#endif

                                    // update the deleted range
                                    range.first += static_cast<int>(vb_size.second);
                                }
                            }
                            break;
                        }
                    }
                    if (!vb_found) {
                        spdlog::get("illixr")->error(
                            "Should not happen: Can not find packed vb despite found the hash entry in allocate_new_VB_");
                    }
                    packed = true;
                }
                break;
            }
        }

        // if existing deleted range cannot fit, append it to the end like the original method
        if (!packed) {
            auto     packing_vb = vb_size.first;
            unsigned hash_idx   = hash_vb(packing_vb);
            auto     alloc_it   = allocate_new_VB_.find(hash_idx);
#ifndef NDEBUG
            spdlog::get("illixr")->debug("adding %d faces to the end", vb_size.second);
#endif
            new_faces += vb_size.second;

            if (alloc_it == allocate_new_VB_.end()) {
                // This shouldn't happen
                spdlog::get("illixr")->error("Should not happen:Can not find packed vb");
            } else {
                bool vb_found = false;
                for (auto& vb_entry : alloc_it->second) {
                    VoxelBlockIndex cur_vb = std::get<0>(vb_entry);
                    if (cur_vb == packing_vb) {
                        auto vb_vertices = std::get<1>(vb_entry);

                        // create new entry in MashVBToRange
                        auto new_tuple = std::make_tuple(cur_vb, static_cast<int>(vertices_.size() / 3),
                                                         static_cast<int>(vertices_.size() / 3 + vb_size.second - 1));
#ifndef NDEBUG
                        spdlog::get("illixr")->debug("allocating new VB: %d, %d, %d in map_VB_to_range_ with range %d, %d",
                                                     std::get<0>(vb_size.first), std::get<1>(vb_size.first),
                                                     std::get<2>(vb_size.first), std::get<1>(new_tuple),
                                                     std::get<2>(new_tuple));
#endif
                        // 923 handle hash collision
                        auto map_it = map_VB_to_range_.find(hash_idx);
                        if (map_it == map_VB_to_range_.end()) {
                            std::vector<Vector_range> hash_entry(1, new_tuple);
                            map_VB_to_range_[hash_idx] = std::move(hash_entry);
#ifndef NDEBUG
                            spdlog::get("illixr")->debug("new VB not found in Mapping, creating a new entry");
#endif
                        } else {
                            bool found = false;
                            for (auto& map_vb_entry : map_it->second) {
                                VoxelBlockIndex cur_map_vb = std::get<0>(map_vb_entry);
                                if (cur_map_vb == packing_vb) {
#ifndef NDEBUG
                                    spdlog::get("illixr")->debug(
                                        "appending to the end VB: %d, %d, %d, found existing entry with range %d, %d",
                                        std::get<0>(packing_vb), std::get<1>(packing_vb), std::get<2>(packing_vb),
                                        std::get<1>(map_vb_entry), std::get<2>(map_vb_entry));
#endif
                                    // update the new range
                                    std::get<1>(map_vb_entry) = std::get<1>(new_tuple);
                                    std::get<2>(map_vb_entry) = std::get<2>(new_tuple);

#ifndef NDEBUG
                                    spdlog::get("illixr")->debug(
                                        "appending to the end VB: %d, %d, %d, updated entry with range %d, %d",
                                        std::get<0>(packing_vb), std::get<1>(packing_vb), std::get<2>(packing_vb),
                                        std::get<1>(map_vb_entry), std::get<2>(map_vb_entry));
#endif

                                    found = true;
                                    break;
                                }
                            }
                            if (!found) {
                                map_VB_to_range_[hash_idx].push_back(std::move(new_tuple));
                            }
                        }
#ifndef NDEBUG
                        // Print all VBs sharing this hash index
                        spdlog::get("illixr")->debug("Hash index %u shared by the following Voxel Blocks:", hash_idx);
                        for (auto& test_vb_entry : map_VB_to_range_[hash_idx]) {
                            VoxelBlockIndex test_vb = std::get<0>(test_vb_entry);
                            spdlog::get("illixr")->debug("  VB: %d, %d, %d with range [%d, %d]", std::get<0>(test_vb),
                                                         std::get<1>(test_vb), std::get<2>(test_vb), std::get<1>(test_vb_entry),
                                                         std::get<2>(test_vb_entry));
                        }
#endif

                        vertices_.insert(vertices_.end(), std::make_move_iterator(vb_vertices.begin()),
                                         std::make_move_iterator(vb_vertices.end())); // Changed back to move
                        vb_found = true;
                        break;
                    }
                }
                if (!vb_found) {
                    spdlog::get("illixr")->error("Should not happen: Can not find packed vb despite founding the hash entry");
                }
            }
        }
    }

    // Clean up the used deleted ranges
    std::vector<std::pair<int, int>> remaining_deleted_ranges;
    for (const auto& range : deleted_ranges_) {
        if (range.first > range.second) {
#ifndef NDEBUG
            spdlog::get("illixr")->debug("fully used range: %d, %d", range.first, range.second);
#endif
            continue;
        }
        int range_size = range.second - range.first + 1;
        // if it can still fit more vbs
        if (range_size > 0) {
            remaining_deleted_ranges.push_back(range);
        }
    }

#ifndef NDEBUG
    spdlog::get("illixr")->debug("unfilled range # %zu", remaining_deleted_ranges.size());
#endif

    unsigned counter   = 0;
    unsigned total_gap = 0;
    for (const auto& range : remaining_deleted_ranges) {
#ifndef NDEBUG
        spdlog::get("illixr")->debug("unfilled range %u, %d, %d", counter, range.first, range.second);
#endif
        total_gap += range.second - range.first + 1;
        counter++;
    }

    // mesh nullification (S4.3 stage 4)
    for (const auto& remaining_range : remaining_deleted_ranges) {
        std::vector<int> saved_face_vector(std::make_move_iterator(faces_.begin() + remaining_range.first * 3),
                                           std::make_move_iterator(faces_.begin() + (remaining_range.second + 1) * 3));
        auto saved_range = std::make_tuple(remaining_range.first, remaining_range.second, std::move(saved_face_vector));
        nullified_ranges.push_back(std::move(saved_range));
#ifndef NDEBUG
        spdlog::get("illixr")->debug("nullifying %u to %u", remaining_range.first, remaining_range.second + 1);
#endif
        std::fill(faces_.begin() + remaining_range.first * 3, faces_.begin() + (remaining_range.second + 1) * 3, 0);
    }

    faces_.insert(faces_.end(), faces_base_.begin() + static_cast<long>(faces_.size()),
                  faces_base_.begin() + static_cast<long>(faces_.size()) + new_faces * 3);

#ifndef NDEBUG
// Eigen::Vector3i last_face = faces_.back();
// spdlog::get("illixr")->debug("Last face %zu: [%d, %d, %d]", faces_.size() / 3, faces_[faces_.size() - 3],
// faces_[faces_.size() - 2],
//       faces_[faces_.size() - 1]);
#endif

    // update with the deleted range
    std::swap(remaining_deleted_ranges, deleted_ranges_);
    allocate_new_VB_.clear();
#ifndef NDEBUG
    spdlog::get("illixr")->debug("added %u new faces to the end of existing mesh", new_faces);
    unsigned vb_count = 0;
    for (auto& map_it : map_VB_to_range_) {
        for ([[maybe_unused]] auto& vector_it : map_it.second) {
            vb_count++;
        }
    }
    spdlog::get("illixr")->debug("vb_count at the end %u", vb_count);
#endif

    return total_gap;
}

[[maybe_unused]] void spatial_hash::print_mesh_as_obj(unsigned id, unsigned type, const std::string& sub_str) {
    (void)type;
    (void)sub_str;
    std::string   filename = std::to_string(id) + ".obj";
    std::ofstream out_file(filename);

    // Print vertices with colors
    for (const auto & vertex : vertices_) {
        out_file << "v " << vertex.x() << " " << vertex.y() << " " << vertex.z() << "";
    }
    spdlog::get("illixr")->info("Output Mesh has %lu faces", faces_.size());
    for (size_t i = 0; i < faces_.size(); i += 3) {
        out_file << "f " << faces_[i] + 1 << " " << faces_[i + 1] + 1 << " " << faces_[i + 2] + 1 << "";
    }
    out_file.close();
    spdlog::get("illixr")->info("Mesh successfully written to {}", filename);
}
