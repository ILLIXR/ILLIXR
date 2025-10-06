#pragma once

#include "draco_illixr/mesh/mesh.h"
#include "illixr/switchboard.hpp"

namespace ILLIXR::data_format {

using VoxelBlockIndex = std::tuple<int, int, int>;
using NewVB           = std::tuple<VoxelBlockIndex, std::vector<Eigen::Vector3d>, std::vector<Eigen::Vector3d>>;

struct draco_type : public switchboard::event {
    std::unique_ptr<draco_illixr::Mesh>              preprocessed_mesh;
    unsigned                                         frame_id;
    unsigned                                         num_vertices;
    unsigned                                         num_faces;
    unsigned                                         chunk_id;
    unsigned                                         max_chunk;
    std::vector<Eigen::Vector3d>                     vertices;
    std::vector<Eigen::Vector3d>                     colors;
    std::unordered_map<unsigned, std::vector<NewVB>> scene_update_mapping;
    unsigned                                         face_number;

    // 91 for moving scene update mapping
    draco_type(unsigned id, unsigned chunk_id_, std::unordered_map<unsigned, std::vector<NewVB>>&& inputUpdateMap)
        : frame_id(id)
        , chunk_id{chunk_id_}
        , scene_update_mapping(std::move(inputUpdateMap)) { }

    // 527 for moving generatePartialMesh
    draco_type(std::unique_ptr<draco_illixr::Mesh> input_mesh, unsigned id, std::vector<Eigen::Vector3d>& vertices_,
               std::vector<Eigen::Vector3d>& colors_, unsigned face_number_)
        : preprocessed_mesh(std::move(input_mesh))
        , frame_id(id)
        , vertices{vertices_}
        , colors{colors_}
        , face_number{face_number_} { }

    draco_type(std::unique_ptr<draco_illixr::Mesh> input_mesh, unsigned id)
        : preprocessed_mesh(std::move(input_mesh))
        , frame_id(id) { }

    draco_type(std::unique_ptr<draco_illixr::Mesh> input_mesh, unsigned id, unsigned chunk_id_, unsigned max_chunk_)
        : preprocessed_mesh(std::move(input_mesh))
        , frame_id(id)
        , chunk_id(chunk_id_)
        , max_chunk(max_chunk_) { }

    draco_type(std::unique_ptr<draco_illixr::Mesh> input_mesh, unsigned id, unsigned num_vertices_, unsigned num_faces_,
               unsigned chunk_id_, unsigned max_chunk_)
        : preprocessed_mesh(std::move(input_mesh))
        , frame_id(id)
        , num_vertices(num_vertices_)
        , num_faces(num_faces_)
        , chunk_id(chunk_id_)
        , max_chunk(max_chunk_) { }
};

} // namespace ILLIXR::data_format
