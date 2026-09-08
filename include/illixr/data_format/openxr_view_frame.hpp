#pragma once

#include "illixr/data_format/poses/pose_base.hpp"
#include "illixr/switchboard.hpp"

#include <cstdint>

namespace ILLIXR::data_format {

/**
 * Per-eye view sampled from an OpenXR PRIMARY_STEREO view configuration.
 * The inherited eye pose is in the OpenXR LOCAL reference space.
 */
struct openxr_eye_view : public pose::pose_base {
#ifdef USING_OPENXR
    /** Match the Eigen pose_base default with an identity rotation. */
    openxr_eye_view() {
        orientation.w = 1.0F;
    }
#endif

    /** Asymmetric projection angles in radians, following XrFovf semantics. */
    float angle_left{0.0F};
    float angle_right{0.0F};
    float angle_up{0.0F};
    float angle_down{0.0F};

    /** Runtime-recommended swapchain extent for this eye. */
    std::uint32_t recommended_width{0};
    std::uint32_t recommended_height{0};

#ifdef USING_OPENXR
    /** OpenXR's pose_base has no validity field; mirror the Eigen base's flag. */
    bool valid{false};
#endif
    /** Distinguishes live tracking from inference; validity alone permits use. */
    bool pose_tracked{false};
};

/**
 * OpenXR eye views for one predicted display time.
 *
 * The `sequence` and `xr_sample_time` values match the `quest_controller`
 * event sampled in the same OpenXR frame. This lets a renderer consume a
 * coherent head/controller snapshot without owning another OpenXR session.
 */
struct openxr_view_frame : public switchboard::event {
    /** Monotonic ID shared with the controller event sampled in this frame. */
    std::uint64_t sequence{0};
    /** Host and OpenXR timestamps for the same predicted display instant. */
    time_point   sample_time{};
    std::int64_t xr_sample_time{0};
    std::int64_t xr_predicted_display_period{0};
    /** Mirrors XrFrameState::shouldRender for downstream frame production. */
    bool should_render{false};

    openxr_eye_view left;
    openxr_eye_view right;
};

} // namespace ILLIXR::data_format
