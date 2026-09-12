#include "spatial_hash.hpp"

#include <fstream>
#include <spdlog/spdlog.h>

using namespace ILLIXR;

namespace {
// Visit portions of sorted, disjoint inclusive ranges that are not covered by
// the other range list. Both cursors move forward, so the work is linear in
// the number of ranges, independent of the number of faces inside each hole.
template<typename Function>
void for_each_range_difference(const std::vector<std::pair<int, int>>& ranges, const std::vector<std::pair<int, int>>& covered,
                               Function apply) {
    size_t covered_index = 0;
    for (const auto& range : ranges) {
        int first = range.first;
        while (covered_index < covered.size() && covered[covered_index].second < first)
            ++covered_index;
        while (covered_index < covered.size() && covered[covered_index].first <= range.second) {
            const auto& overlap = covered[covered_index];
            if (first < overlap.first)
                apply(first, overlap.first - 1);
            first = std::max(first, overlap.second + 1);
            if (first > range.second)
                break;
            ++covered_index;
        }
        if (first <= range.second)
            apply(first, range.second);
    }
}
} // namespace

spatial_hash::spatial_hash() {
    // map_VB_to_range_.reserve(1000000);
    map_VB_to_range_.reserve(25600);
    deleted_ranges_.reserve(10000);

    vertices_.reserve(5000000);
    // colors.reserve(5000000);
    faces_.reserve(1000000);

    nullified_ranges_.reserve(100000);
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

template<typename Map>
void spatial_hash::append_mesh_allocate_impl(std::shared_ptr<const Map> inputSceneUpdateMap) {
    const auto& mapping = *inputSceneUpdateMap;
    input_owners_.push_back(std::move(inputSceneUpdateMap));
    for (const auto& pair : mapping) {
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
                        found_vb              = true;
                        auto& merged_vertices = std::get<1>(vb_entry);

                        merged_vertices.append(cur_vertices);
                        break;
                    }
                }
                if (!found_vb) {
                    VertexFragments fragments;
                    fragments.append(cur_vertices);
                    it->second.emplace_back(cur_vb, std::move(fragments));
                }
            }
        } else {
            std::vector<PendingVB> blocks;
            blocks.reserve(new_vbs.size());
            for (const auto& new_vb : new_vbs) {
                VertexFragments fragments;
                fragments.append(std::get<1>(new_vb));
                blocks.emplace_back(std::get<0>(new_vb), std::move(fragments));
            }
            allocate_new_VB_[hash_idx] = std::move(blocks);
        }
    }
}

void spatial_hash::append_mesh_allocate(std::shared_ptr<const SceneUpdateMap> inputSceneUpdateMap) {
    append_mesh_allocate_impl(std::move(inputSceneUpdateMap));
}

void spatial_hash::append_mesh_allocate(std::shared_ptr<const SceneUpdateRanges> inputSceneUpdateMap) {
    append_mesh_allocate_impl(std::move(inputSceneUpdateMap));
}

// pyh merge is for a feature we never used in publication, so ignore
unsigned spatial_hash::append_mesh_match_and_insert(bool merge) {
    (void) merge;
    pending_placements_.clear();
    for (const auto& entry : allocate_new_VB_) {
        for (const auto& block : entry.second) {
            pending_placements_.push_back({&block, entry.first, static_cast<unsigned>(std::get<1>(block).size() / 3)});
        }
    }
    // Preserve the original traversal, size-only comparator and largest-first
    // ordering, including the ordering of equally sized blocks.
    std::sort(pending_placements_.begin(), pending_placements_.end(), [](const PendingPlacement& a, const PendingPlacement& b) {
        return a.faces > b.faces;
    });

    unsigned new_faces = 0;
    if (!pending_placements_.empty())
        free_ranges_.rebuild(deleted_ranges_);

    for (const auto& placement : pending_placements_) {
        const auto& block_index = std::get<0>(*placement.block);
        const auto& vertices    = std::get<1>(*placement.block);
        const auto  range_index = free_ranges_.first_fit(placement.faces);
        const bool  packed      = range_index < deleted_ranges_.size();
        const int   first       = packed ? deleted_ranges_[range_index].first : static_cast<int>(vertices_.size() / 3);
        const int   last        = first + static_cast<int>(placement.faces) - 1;

        if (packed) {
            vertices.copy_to(vertices_.begin() + first * 3);
        } else {
            vertices.append_to(vertices_);
            new_faces += placement.faces;
        }

        // The sorted item already identifies the pending block and its hash;
        // only the persistent block-to-scene mapping needs a lookup here.
        auto map_it = map_VB_to_range_.find(placement.hash);
        if (map_it == map_VB_to_range_.end()) {
            std::vector<Vector_range> hash_entry(1, std::make_tuple(block_index, first, last));
            map_VB_to_range_[placement.hash] = std::move(hash_entry);
        } else {
            bool found = false;
            for (auto& mapped : map_it->second) {
                if (std::get<0>(mapped) == block_index) {
                    if (packed && (std::get<1>(mapped) != -1 || std::get<2>(mapped) != -1)) {
                        spdlog::get("illixr")->error("Updated voxel block still has an occupied scene range");
                    }
                    std::get<1>(mapped) = first;
                    std::get<2>(mapped) = last;
                    found               = true;
                    break;
                }
            }
            if (!found)
                map_it->second.emplace_back(block_index, first, last);
        }

        if (packed) {
            auto& range = deleted_ranges_[range_index];
            range.first += static_cast<int>(placement.faces);
            free_ranges_.update(range_index, static_cast<unsigned>(range.second - range.first + 1));
        }
    }

    // Clean up the used deleted ranges
    auto& remaining_deleted_ranges = remaining_deleted_ranges_;
    remaining_deleted_ranges.clear();
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

    // Restore only old holes that were filled, and nullify only newly unused
    // faces. Persistent holes remain zero, including when ranges split or merge.
    for_each_range_difference(nullified_ranges_, remaining_deleted_ranges, [this](int first, int last) {
        std::copy(faces_base_.begin() + first * 3, faces_base_.begin() + (last + 1) * 3, faces_.begin() + first * 3);
    });
    for_each_range_difference(remaining_deleted_ranges, nullified_ranges_, [this](int first, int last) {
        std::fill(faces_.begin() + first * 3, faces_.begin() + (last + 1) * 3, 0);
    });
    nullified_ranges_.assign(remaining_deleted_ranges.begin(), remaining_deleted_ranges.end());

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
    pending_placements_.clear();
    allocate_new_VB_.clear();
    input_owners_.clear();
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

// pyh verified previously missing \n
[[maybe_unused]] void spatial_hash::print_mesh_as_obj(unsigned id, unsigned type, const std::string& sub_str) {
    (void) type;
    (void) sub_str;
    std::string   filename = std::to_string(id) + ".obj";
    std::ofstream out_file(filename);

    // Print vertices with colors
    for (const auto& vertex : vertices_) {
        out_file << "v " << vertex.x() << " " << vertex.y() << " " << vertex.z() << "\n";
    }
    spdlog::get("illixr")->info("Output Mesh has %lu faces", faces_.size());
    for (size_t i = 0; i < faces_.size(); i += 3) {
        out_file << "f " << faces_[i] + 1 << " " << faces_[i + 1] + 1 << " " << faces_[i + 2] + 1 << "\n";
    }
    out_file.close();
    spdlog::get("illixr")->info("Mesh successfully written to {}", filename);
}
