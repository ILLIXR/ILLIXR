#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <draco_illixr/core/hash_utils.h>
#include <draco_illixr/mesh/mesh.h>
#include <limits>
#include <memory>
#include <unordered_map>
#include <vector>

namespace ILLIXR {

struct open3d_block_dictionary_hash {
    size_t operator()(const std::array<int32_t, 3>& block) const {
        return (uint32_t(block[0]) * 73856093u) ^ (uint32_t(block[1]) * 19349669u) ^ (uint32_t(block[2]) * 83492791u);
    }
};

struct open3d_point_dictionary_hash {
    size_t operator()(const std::array<uint32_t, 2>& attributes) const {
        uint32_t hash = 0;
        for (const auto value : attributes)
            hash = static_cast<uint32_t>(draco_illixr::HashCombine(value, hash));
        return hash;
    }
};

// Follow Draco's DeduplicateFormattedValues and DeduplicatePointIds: compare
// exact attribute bits, keep first-occurrence IDs, and identify a point by all
// its attribute-value indices. Build each independently owned chunk directly
// in that form so the compression worker can proceed to encoding.
inline std::unique_ptr<draco_illixr::Mesh> make_open3d_draco_mesh(const float* vertices, const int* triangles,
                                                                  const int* block_tags, unsigned first_triangle,
                                                                  unsigned num_faces) {
    using namespace draco_illixr;
    if (num_faces > std::numeric_limits<PointIndex::ValueType>::max() / 3) {
        return nullptr;
    }
    const unsigned num_vertices = num_faces * 3;
    auto           mesh         = std::make_unique<Mesh>();
    mesh->SetNumFaces(num_faces);
    mesh->set_num_points(num_vertices);

    GeometryAttribute position;
    position.Init(GeometryAttribute::POSITION, nullptr, 3, DT_FLOAT32, false, sizeof(float) * 3, 0);
    const int position_id = mesh->AddAttribute(position, false, num_vertices);
    if (position_id < 0) {
        return nullptr;
    }
    auto* positions    = mesh->attribute(position_id);
    auto  voxel_blocks = std::make_unique<PointAttribute>();
    voxel_blocks->Init(GeometryAttribute::GENERIC, 1, DT_INT32, false, num_faces);
    voxel_blocks->SetExplicitMapping(num_vertices);
    using PositionBits = std::array<uint32_t, 3>;
    using PointValues  = std::array<uint32_t, 2>;
    std::unordered_map<PositionBits, AttributeValueIndex, HashArray<PositionBits>>    position_ids;
    std::unordered_map<PointValues, PointIndex, open3d_point_dictionary_hash>         point_ids(num_vertices);
    std::unordered_map<std::array<int32_t, 3>, int32_t, open3d_block_dictionary_hash> block_ids;
    std::vector<int32_t>                                                              dictionary;
    block_ids.reserve(256);
    dictionary.reserve(256 * 3);

    for (unsigned face = 0; face < num_faces; ++face) {
        const unsigned               offset = (first_triangle + face) * 3;
        const std::array<int32_t, 3> block{block_tags[offset], block_tags[offset + 1], block_tags[offset + 2]};
        const auto                   next_id  = static_cast<int32_t>(dictionary.size() / 3);
        const auto                   inserted = block_ids.try_emplace(block, next_id);
        if (inserted.second) {
            dictionary.insert(dictionary.end(), block.begin(), block.end());
            voxel_blocks->SetAttributeValue(AttributeValueIndex(next_id), &next_id);
        }
        const uint32_t block_value = static_cast<uint32_t>(inserted.first->second);
        PointIndex     points[3];
        for (unsigned corner = 0; corner < 3; ++corner) {
            const auto* corner_position = vertices + triangles[offset + corner] * 3;
            // Open3D extracts in meters; Ada transports and stores centimeters.
            const float  value[] = {corner_position[0] * 100.0f, corner_position[1] * 100.0f, corner_position[2] * 100.0f};
            PositionBits bits;
            std::memcpy(bits.data(), value, sizeof(value));
            auto position_it = position_ids.find(bits);
            if (position_it == position_ids.end()) {
                const AttributeValueIndex id(static_cast<uint32_t>(position_ids.size()));
                position_it = position_ids.emplace(bits, id).first;
                positions->SetAttributeValue(id, value);
            }
            const PointValues values{position_it->second.value(), block_value};
            auto              point_it = point_ids.find(values);
            if (point_it == point_ids.end()) {
                const PointIndex id(static_cast<uint32_t>(point_ids.size()));
                point_it = point_ids.emplace(values, id).first;
                positions->SetPointMapEntry(id, position_it->second);
                voxel_blocks->SetPointMapEntry(id, AttributeValueIndex(block_value));
            }
            points[corner] = point_it->second;
        }
        // Preserve the Open3D triangle winding, already supplied by the extractor.
        mesh->SetFace(FaceIndex(face), {points[0], points[1], points[2]});
    }

    const auto point_count = static_cast<uint32_t>(point_ids.size());
    mesh->set_num_points(point_count);
    // Draco leaves an empty position attribute without a backing buffer.
    if (num_vertices != 0)
        positions->Resize(position_ids.size());
    if (position_ids.size() == num_vertices)
        positions->SetIdentityMapping();
    else
        positions->SetExplicitMapping(point_count);
    voxel_blocks->Resize(dictionary.size() / 3);
    voxel_blocks->SetExplicitMapping(point_count);
    const int block_id = mesh->AddAttribute(std::move(voxel_blocks));
    if (block_id < 0) {
        return nullptr;
    }
    auto metadata = std::make_unique<AttributeMetadata>();
    // IDs and exact coordinates belong to this chunk. Keep the dictionary in
    // its Draco payload so decoding never depends on a separate message.
    metadata->AddEntryString("attribute_name", "_VOXELBLOCK_ID");
    metadata->AddEntryInt("ada_block_dictionary_version", 1);
    if (!dictionary.empty()) {
        metadata->AddEntryIntArray("ada_block_dictionary", dictionary);
    }
    mesh->AddAttributeMetadata(block_id, std::move(metadata));
    return mesh;
}

} // namespace ILLIXR
