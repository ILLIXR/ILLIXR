#pragma once

#include "draco_illixr/mesh/mesh.h"
#include "illixr/data_format/scene_update.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>

namespace ILLIXR {

class mesh_formatter {
    using SceneData   = data_format::scene_update_data;
    using VertexRange = data_format::scene_vertex_range;
    using BlockIndex  = std::tuple<int, int, int>;

    struct Chunk : SceneData {
        std::vector<uint32_t>     face_blocks;
        std::vector<VertexRange*> ranges;
        std::vector<size_t>       cursors;
    };

    struct ReturnedChunks {
        std::mutex                          mutex;
        std::vector<std::unique_ptr<Chunk>> chunks;

        void recycle(std::unique_ptr<Chunk> chunk) noexcept {
            try {
                std::lock_guard<std::mutex> lock(mutex);
                chunks.push_back(std::move(chunk));
            } catch (const std::bad_alloc&) {
                // Reuse is optional if retaining the chunk would require an
                // allocation that cannot be satisfied. Its storage is freed.
            }
        }
    };

    std::shared_ptr<ReturnedChunks> returned_ = std::make_shared<ReturnedChunks>();

    static unsigned hash_vb(const BlockIndex& index) {
        int x, y, z;
        std::tie(x, y, z) = index;
        auto hash         = (x * 73856093) ^ (y * 19349669) ^ (z * 83492791);
        return static_cast<unsigned>(std::abs(hash) % 25600);
    }

public:
    std::shared_ptr<const SceneData> format(const draco_illixr::Mesh& mesh) {
        std::unique_ptr<Chunk> owned;
        {
            std::lock_guard<std::mutex> lock(returned_->mutex);
            if (!returned_->chunks.empty()) {
                owned = std::move(returned_->chunks.back());
                returned_->chunks.pop_back();
            }
        }
        if (!owned)
            owned = std::make_unique<Chunk>();
        auto  chunk  = std::shared_ptr<Chunk>(owned.release(), [returned = returned_](Chunk* p) {
            returned->recycle(std::unique_ptr<Chunk>(p));
        });
        auto& output = chunk->block_ranges;
        output.clear();
        output.reserve(256);

        const auto* positions = mesh.GetNamedAttribute(draco_illixr::GeometryAttribute::POSITION);
        const int   vb_id     = mesh.GetAttributeIdByMetadataEntry("attribute_name", "_VOXELBLOCK_INFO");
        const auto* vb        = mesh.GetAttributeByUniqueId(vb_id);
        if (!positions || !vb)
            throw std::runtime_error("Decoded mesh lacks positions or voxel block attributes");

        const size_t face_count = mesh.num_faces();
        const size_t required   = face_count * 3;
        // The returned chunk has no readers. Clear its logical size before
        // reserving so growth never copies coordinates from an earlier update.
        chunk->vertices.clear();
        if (required > chunk->vertices.capacity()) {
            chunk->vertices.reserve(std::max(required, chunk->vertices.capacity() + chunk->vertices.capacity() / 2));
        }
        chunk->vertices.resize(required);
        chunk->face_blocks.clear();
        chunk->face_blocks.reserve(face_count);

        size_t block_count = 0;
        for (draco_illixr::FaceIndex f(0); f < mesh.num_faces(); ++f) {
            int tag[3];
            vb->GetMappedValue(mesh.face(f)[0], tag);
            BlockIndex index{tag[0], tag[1], tag[2]};
            auto&      bucket   = output[hash_vb(index)];
            bool       appended = false;
            for (auto& block : bucket) {
                if (std::get<0>(block) == index) {
                    auto& range = std::get<1>(block);
                    range.count += 3;
                    chunk->face_blocks.push_back(range.offset);
                    appended = true;
                    break;
                }
            }
            if (!appended) {
                // While counting, offset holds a dense block ID. Bucket
                // vectors may grow, so faces record IDs rather than pointers.
                bucket.emplace_back(index, VertexRange{nullptr, block_count, 3}, VertexRange{});
                chunk->face_blocks.push_back(block_count++);
            }
        }

        chunk->ranges.resize(block_count);
        chunk->cursors.assign(block_count, 0);
        size_t offset = 0;
        for (auto& bucket : output) {
            for (auto& block : bucket.second) {
                auto& range                 = std::get<1>(block);
                chunk->ranges[range.offset] = &range;
                range.storage               = &chunk->vertices;
                range.offset                = offset;
                offset += range.count;
            }
        }
        for (draco_illixr::FaceIndex f(0); f < mesh.num_faces(); ++f) {
            const auto  id     = chunk->face_blocks[f.value()];
            const auto* range  = chunk->ranges[id];
            size_t      target = range->offset + chunk->cursors[id];
            const auto& face   = mesh.face(f);
            for (int v = 0; v < 3; ++v) {
                float p[3];
                positions->GetMappedValue(face[v], p);
                chunk->vertices[target++] = data_format::scene_vertex(p[0], p[1], p[2]);
            }
            chunk->cursors[id] += 3;
        }
        return chunk;
    }
};

} // namespace ILLIXR
