// In illixr/data_format/pose_id.hpp
#pragma once
#include "illixr/switchboard.hpp"

#include <cstdint>

namespace ILLIXR::data_format {

/// Published by illixr_component when a frame is released to the encoder,
/// carrying the combined_pose id that was used to render that frame.
/// Consumed by offload_rendering_server to tag compressed_frame::pose_id.
struct frame_pose_id : public switchboard::event {
    uint64_t pose_id{0};     ///< combined_pose id matched to the released frame
    uint64_t frame_index{0}; ///< buffer pool index for correlation

    frame_pose_id() = default;

    frame_pose_id(uint64_t id, uint64_t idx)
        : pose_id{id}
        , frame_index{idx} { }
};

} // namespace ILLIXR::data_format