#pragma once

#include "illixr/switchboard.hpp"

#include <Eigen/Core>
#include <Eigen/Geometry>
#include <cstdint>

namespace ILLIXR::data_format {

/** Per-eye view sampled from an OpenXR PRIMARY_STEREO view configuration. */
struct openxr_eye_view {
    Eigen::Vector3f    position{Eigen::Vector3f::Zero()};
    Eigen::Quaternionf orientation{Eigen::Quaternionf::Identity()};

    float angle_left{0.0F};
    float angle_right{0.0F};
    float angle_up{0.0F};
    float angle_down{0.0F};

    std::uint32_t recommended_width{0};
    std::uint32_t recommended_height{0};

    bool pose_valid{false};
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
    std::uint64_t sequence{0};
    time_point    sample_time{};
    std::int64_t  xr_sample_time{0};
    std::int64_t  xr_predicted_display_period{0};
    bool          should_render{false};

    openxr_eye_view left;
    openxr_eye_view right;
};

} // namespace ILLIXR::data_format
