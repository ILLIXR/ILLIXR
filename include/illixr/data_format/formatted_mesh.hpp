#pragma once

#include "illixr/data_format/draco.hpp"
#include "illixr/data_format/scene_update.hpp"

namespace ILLIXR::data_format {

// Retain the existing Draco event metadata while owning the grouped chunk's
// contiguous storage. Readers retain this ownership when keeping block ranges.
struct formatted_mesh_type : public draco_type {
    std::shared_ptr<const scene_update_data> data;

    formatted_mesh_type(unsigned frame, unsigned chunk, std::shared_ptr<const scene_update_data> input)
        : draco_type(frame, chunk, std::unordered_map<unsigned, std::vector<NewVB>>{})
        , data(std::move(input)) { }
};

} // namespace ILLIXR::data_format
