#pragma once

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

namespace ILLIXR {
// Maximum remaining capacity over consecutive groups of free ranges. Leaves
// retain the input order, so a left-first search preserves first-fit placement.
class free_range_index {
public:
    void rebuild(const std::vector<std::pair<int, int>>& ranges) {
        range_count_ = ranges.size();
        leaf_base_   = 1;
        while (leaf_base_ < range_count_)
            leaf_base_ *= 2;
        // Retain storage across scene updates, clearing unused/padded leaves.
        maximum_.assign(leaf_base_ * 2, 0);
        for (size_t i = 0; i < range_count_; ++i) {
            const auto& range = ranges[i];
            if (range.first <= range.second)
                maximum_[leaf_base_ + i] = static_cast<unsigned>(range.second - range.first + 1);
        }
        for (size_t node = leaf_base_ - 1; node > 0; --node)
            maximum_[node] = std::max(maximum_[node * 2], maximum_[node * 2 + 1]);
    }

    // Return ranges.size() if none fits. Even an empty incoming block must skip
    // exhausted ranges, matching the allocator's existing first-fit scan.
    size_t first_fit(unsigned faces) const {
        const auto required = std::max(faces, 1U);
        if (maximum_.empty() || maximum_[1] < required)
            return range_count_;
        size_t node = 1;
        while (node < leaf_base_) {
            node *= 2;
            if (maximum_[node] < required)
                ++node;
        }
        return node - leaf_base_;
    }

    void update(size_t range, unsigned remaining_faces) {
        size_t node    = leaf_base_ + range;
        maximum_[node] = remaining_faces;
        while (node > 1) {
            node /= 2;
            const auto remaining = std::max(maximum_[node * 2], maximum_[node * 2 + 1]);
            if (maximum_[node] == remaining)
                break;
            maximum_[node] = remaining;
        }
    }

private:
    size_t                range_count_ = 0;
    size_t                leaf_base_   = 1;
    std::vector<unsigned> maximum_;
};
} // namespace ILLIXR
