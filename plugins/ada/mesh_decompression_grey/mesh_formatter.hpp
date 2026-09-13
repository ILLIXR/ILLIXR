#pragma once

#include "draco_illixr/mesh/mesh.h"
#include "illixr/data_format/scene_update.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <limits>
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
    // Scratch storage belongs to this worker. Every chunk replaces its contents.
    std::vector<int32_t>  dictionary_;
    std::vector<uint32_t> dictionary_groups_;

    static unsigned hash_vb(const BlockIndex& index) {
        int x, y, z;
        std::tie(x, y, z) = index;
        auto hash         = (x * 73856093) ^ (y * 19349669) ^ (z * 83492791);
        return static_cast<unsigned>(std::abs(hash) % 25600);
    }

    size_t group_dictionary(const draco_illixr::Mesh& mesh, int attribute_id, Chunk& chunk) {
        const auto* ids      = mesh.attribute(attribute_id);
        const auto* metadata = mesh.GetAttributeMetadataByAttributeId(attribute_id);
        int32_t     version  = 0;
        if (ids->data_type() != draco_illixr::DT_INT32 || ids->num_components() != 1 || !metadata ||
            !metadata->GetEntryInt("ada_block_dictionary_version", &version) || version != 1) {
            throw std::runtime_error("Invalid voxel block dictionary format");
        }
        dictionary_.clear();
        const bool has_dictionary = metadata->entries().count("ada_block_dictionary") != 0;
        if ((has_dictionary && !metadata->GetEntryIntArray("ada_block_dictionary", &dictionary_)) ||
            (!has_dictionary && mesh.num_faces() != 0) || dictionary_.size() % 3 != 0) {
            throw std::runtime_error("Invalid voxel block dictionary coordinates");
        }
        const auto unresolved = std::numeric_limits<uint32_t>::max();
        dictionary_groups_.assign(dictionary_.size() / 3, unresolved);
        chunk.cursors.clear();
        size_t block_count = 0;
        for (draco_illixr::FaceIndex f(0); f < mesh.num_faces(); ++f) {
            int32_t block_id;
            ids->GetMappedValue(mesh.face(f)[0], &block_id);
            if (block_id < 0 || static_cast<size_t>(block_id) >= dictionary_groups_.size()) {
                throw std::runtime_error("Voxel block ID is outside its chunk dictionary");
            }
            auto& group = dictionary_groups_[block_id];
            if (group == unresolved) {
                const size_t start = static_cast<size_t>(block_id) * 3;
                BlockIndex   index{dictionary_[start], dictionary_[start + 1], dictionary_[start + 2]};
                auto&        bucket = chunk.block_ranges[hash_vb(index)];
                // Resolve coordinates only on this ID's first face. Preserve
                // collision handling and the original first-face group order.
                for (auto& block : bucket) {
                    if (std::get<0>(block) == index) {
                        group = std::get<1>(block).offset;
                        break;
                    }
                }
                if (group == unresolved) {
                    group = block_count++;
                    bucket.emplace_back(index, VertexRange{nullptr, group, 0}, VertexRange{});
                    chunk.cursors.push_back(0);
                }
            }
            chunk.cursors[group] += 3;
            chunk.face_blocks.push_back(group);
        }
        for (auto& bucket : chunk.block_ranges) {
            for (auto& block : bucket.second) {
                auto& range = std::get<1>(block);
                range.count = chunk.cursors[range.offset];
            }
        }
        return block_count;
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

        // A native empty Draco mesh has no attributes. It still completes this
        // chunk; the extraction's block list removes any disappearing geometry.
        if (mesh.num_faces() == 0) {
            chunk->vertices.clear();
            chunk->face_blocks.clear();
            chunk->ranges.clear();
            chunk->cursors.clear();
            return chunk;
        }

        const auto* positions = mesh.GetNamedAttribute(draco_illixr::GeometryAttribute::POSITION);
        const int   ids_id    = mesh.GetAttributeIdByMetadataEntry("attribute_name", "_VOXELBLOCK_ID");
        const int   vb_id     = mesh.GetAttributeIdByMetadataEntry("attribute_name", "_VOXELBLOCK_INFO");
        const auto* vb        = vb_id < 0 ? nullptr : mesh.attribute(vb_id);
        if (!positions || (ids_id < 0 && !vb))
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
        if (ids_id >= 0) {
            block_count = group_dictionary(mesh, ids_id, *chunk);
        } else {
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
