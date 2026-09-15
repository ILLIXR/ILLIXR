#pragma once

#include <map>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace ILLIXR {

// Decoder workers and VB messages may complete in any order. Only release a
// complete scene, including its matching cleanup, in extraction order (from 0).
// The caller serializes access, including applying each released update.
template<typename ChunkPtr, typename VbPtr>
class ordered_scene_updates {
public:
    struct update {
        unsigned              scene_id;
        VbPtr                 cleanup;
        std::vector<ChunkPtr> chunks;
        unsigned              received = 0;
    };

    explicit ordered_scene_updates(unsigned chunk_count)
        : chunk_count_{chunk_count} {
        if (chunk_count_ == 0) {
            throw std::invalid_argument("PARTIAL_MESH_COUNT must be positive");
        }
    }

    bool add_chunk(unsigned scene_id, unsigned chunk_id, ChunkPtr chunk) {
        if (scene_id < next_scene_ || chunk_id >= chunk_count_ || !chunk) {
            return false;
        }
        auto& scene = get_scene(scene_id);
        if (scene.chunks[chunk_id]) {
            return false;
        }
        scene.chunks[chunk_id] = std::move(chunk);
        ++scene.received;
        return true;
    }

    bool add_cleanup(unsigned scene_id, VbPtr cleanup) {
        if (scene_id < next_scene_ || !cleanup) {
            return false;
        }
        auto& scene = get_scene(scene_id);
        if (scene.cleanup) {
            return false;
        }
        scene.cleanup = std::move(cleanup);
        return true;
    }

    std::optional<update> pop_ready() {
        auto it = pending_.find(next_scene_);
        if (it == pending_.end() || !it->second.cleanup || it->second.received != chunk_count_) {
            return std::nullopt;
        }
        update result = std::move(it->second);
        pending_.erase(it);
        ++next_scene_;
        return result;
    }

private:
    update& get_scene(unsigned scene_id) {
        auto it = pending_.find(scene_id);
        if (it == pending_.end()) {
            it = pending_.emplace(scene_id, update{scene_id, {}, std::vector<ChunkPtr>(chunk_count_), 0}).first;
        }
        return it->second;
    }

    const unsigned             chunk_count_;
    unsigned                   next_scene_ = 0;
    std::map<unsigned, update> pending_;
};

} // namespace ILLIXR
