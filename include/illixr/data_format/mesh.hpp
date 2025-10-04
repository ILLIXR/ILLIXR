#pragma once

#include <draco_illixr/io/ply_reader.h>
#include "illixr/switchboard.hpp"

#include <vector>

namespace ILLIXR::data_format {

struct mesh_type : public switchboard::event {
    unsigned type;
    const std::vector<char> mesh{};
    [[maybe_unused]] bool compressed = false;
    [[maybe_unused]] bool active     = false;
    [[maybe_unused]] unsigned id        = 0;
    [[maybe_unused]] unsigned chunk_id  = 0;
    [[maybe_unused]] unsigned max_chunk = 0;
    // this is additional info for mesh manager
    [[maybe_unused]] unsigned num_faces    = 0;
    [[maybe_unused]] unsigned num_vertices = 0;

    [[maybe_unused]] int cell_x = 0;
    [[maybe_unused]] int cell_y = 0;
    [[maybe_unused]] int cell_z = 0;
    // 12/29 draco integration
    //[[maybe_unused]] std::unique_ptr<draco_illixr::PlyReader> reader;
    [[maybe_unused]] std::shared_ptr<draco_illixr::PlyReader> reader;

    [[maybe_unused]]mesh_type(const unsigned type_, std::shared_ptr<draco_illixr::PlyReader> input_reader, unsigned id_)
            : type{type_}
            , id{id_}
            , reader{std::move(input_reader)} { }

    [[maybe_unused]]mesh_type(const unsigned type_,std::shared_ptr<draco_illixr::PlyReader> input_reader, unsigned id_, bool is_active)
            : type{type_}
            , active{is_active}
            , id{id_}
            , reader{std::move(input_reader)}{ }

    [[maybe_unused]]mesh_type(const unsigned type_, std::shared_ptr<draco_illixr::PlyReader> input_reader, unsigned id_,
                              unsigned chunk_id_, unsigned max_chunk_, unsigned num_faces_, unsigned num_vertices_,
                              bool is_active)
            : type{type_}
            , active{is_active}
            , id{id_}
            , chunk_id{chunk_id_}
            , max_chunk{max_chunk_}
            , num_faces{num_faces_}
            , num_vertices{num_vertices_}
            , reader(std::move(input_reader)){ }

    [[maybe_unused]]mesh_type(const unsigned type_, const std::vector<char>& input_mesh, bool is_compressed, unsigned id_)
            : type{type_}
            , mesh(input_mesh)
            , compressed{is_compressed}
            , id{id_} { }

    // for baseline parallel compression 2
    [[maybe_unused]]mesh_type(const unsigned type_, const std::vector<char>& input_mesh, unsigned id_, unsigned chunk_id_,
                              unsigned max_chunk_)
            : type{type_}
            , mesh(input_mesh)
            , id{id_}
            , chunk_id{chunk_id_}
            , max_chunk{max_chunk_} { }

    [[maybe_unused]]mesh_type(const unsigned type_, const std::vector<char>& input_mesh, bool is_compressed, bool is_active,
                              int cell_x_, int cell_y_, int cell_z_)
            : type{type_}
            , mesh(input_mesh)
            , compressed{is_compressed}
            , active{is_active}
            , cell_x{cell_x_}
            , cell_y{cell_y_}
            , cell_z{cell_z_} { }

    // used by server mesh manager in draco integration
    [[maybe_unused]]mesh_type(const unsigned type_, std::shared_ptr<draco_illixr::PlyReader> input_reader, bool is_compressed,
                              bool is_active, unsigned id_)
            : type{type_}
            , compressed{is_compressed}
            , active{is_active}
            , id{id_}
            , chunk_id{1}
            , max_chunk{1}
            , reader{std::move(input_reader)} { }

    // used by server mesh manager in non-draco integration
    [[maybe_unused]]mesh_type(const unsigned type_, const std::vector<char>& input_mesh, bool is_compressed, bool is_active,
                              unsigned id_)
            : type{type_}
            , mesh(input_mesh)
            , compressed{is_compressed}
            , active{is_active}
            , id{id_}
            , chunk_id{1}
            , max_chunk{1} { }

    [[maybe_unused]]mesh_type(const unsigned type_, const std::vector<char>& input_mesh, bool is_compressed, bool is_active,
                              unsigned id_, unsigned num_faces_, unsigned num_vertices_)
            : type{type_}
            , mesh(input_mesh)
            , compressed{is_compressed}
            , active{is_active}
            , id{id_}
            , num_faces{num_faces_}
            , num_vertices{num_vertices_} { }

    // type to pass to mesh manager
    [[maybe_unused]]mesh_type(const unsigned type_, const std::vector<char>& input_mesh, bool is_compressed, bool is_active,
                              unsigned id_, unsigned chunk_id_, unsigned max_chunk_, unsigned num_faces_, unsigned num_vertices_)
            : type{type_}
            , mesh(input_mesh)
            , compressed{is_compressed}
            , active{is_active}
            , id{id_}
            , chunk_id{chunk_id_}
            , max_chunk{max_chunk_}
            , num_faces{num_faces_}
            , num_vertices{num_vertices_} { }

    [[maybe_unused]]mesh_type(const unsigned type_, const std::vector<char>& input_mesh, bool is_compressed, bool is_active,
                              unsigned id_, unsigned chunk_id_, unsigned max_chunk_, unsigned num_faces_, unsigned num_vertices_,
                              std::shared_ptr<draco_illixr::PlyReader> input_reader)
            : type{type_}
            , mesh(input_mesh)
            , compressed{is_compressed}
            , active{is_active}
            , id{id_}
            , chunk_id{chunk_id_}
            , max_chunk{max_chunk_}
            , num_faces{num_faces_}
            , num_vertices{num_vertices_}
            , reader{std::move(input_reader)} { }

    [[maybe_unused]]mesh_type(const unsigned type_, const std::vector<char>& input_mesh, bool is_active, unsigned id_,
                              unsigned chunk_id_, unsigned max_chunk_)
            : type{type_}
            , mesh(input_mesh)
            , active{is_active}
            , id{id_}
            , chunk_id{chunk_id_}
            , max_chunk{max_chunk_} { }
};

} // namespace ILLIXR::data_format
