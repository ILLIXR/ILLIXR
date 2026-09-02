#pragma once

#include "illixr/switchboard.hpp"

#include <cstdint>
#include <Eigen/Core>
#include <Eigen/Geometry>

namespace ILLIXR::data_format {

/** Interaction profiles currently suggested by the Quest controller plugin. */
enum class quest_controller_profile : std::uint8_t {
    none,
    simple_controller,
    oculus_touch,
    htc_vive,
    valve_index,
    microsoft_motion,
    unknown,
};

/** A controller pose expressed in the OpenXR LOCAL reference space. */
struct quest_controller_pose {
    /** Pose in OpenXR LOCAL space; quaternion storage follows Eigen's API. */
    Eigen::Vector3f    position{Eigen::Vector3f::Zero()};
    Eigen::Quaternionf orientation{Eigen::Quaternionf::Identity()};

    bool active{false};
    bool position_valid{false};
    bool orientation_valid{false};
    bool position_tracked{false};
    bool orientation_tracked{false};

    /** True when both position and orientation may be consumed. */
    [[nodiscard]] bool valid() const {
        return active && position_valid && orientation_valid;
    }

    /** True when both components are currently tracked rather than inferred. */
    [[nodiscard]] bool tracked() const {
        return active && position_tracked && orientation_tracked;
    }
};

/** Boolean or thresholded analog controller input. */
struct quest_controller_button {
    /** Whether the active interaction profile exposes this action source. */
    bool         active{false};
    bool         pressed{false};
    bool         changed_since_last_sync{false};
    float        value{0.0F};
    std::int64_t last_change_time{0}; //!< OpenXR runtime time domain.
};

/** Two-dimensional controller input such as a thumbstick. */
struct quest_controller_axis2d {
    /** Inactive axes always retain the neutral zero value. */
    bool            active{false};
    bool            changed_since_last_sync{false};
    Eigen::Vector2f value{Eigen::Vector2f::Zero()};
    std::int64_t    last_change_time{0}; //!< OpenXR runtime time domain.
};

/** State for one physical hand controller. */
struct quest_hand_controller {
    /** True when at least one OpenXR action source for this hand is active. */
    bool available{false};

    quest_controller_profile interaction_profile{quest_controller_profile::none};

    quest_controller_pose grip_pose;
    quest_controller_pose aim_pose;

    quest_controller_button trigger;
    quest_controller_button squeeze;
    quest_controller_button primary;   //!< X on the left controller, A on the right.
    quest_controller_button secondary; //!< Y on the left controller, B on the right.
    quest_controller_button thumbstick_click;
    quest_controller_axis2d thumbstick;

    [[nodiscard]] bool tracked() const {
        return grip_pose.tracked() || aim_pose.tracked();
    }
};

/**
 * Complete Quest controller snapshot published on the `quest_controller` switchboard topic.
 *
 * Both hand slots are always present. An absent controller has `available == false`, invalid
 * poses, inactive controls, and neutral values. `xr_sample_time` is the OpenXR predicted display
 * time used to locate the poses; `sample_time` is the corresponding ILLIXR clock timestamp.
 */
struct quest_controller_input : public switchboard::event {
    /** Monotonic ID shared with the corresponding `openxr_view` event. */
    std::uint64_t sequence{0};
    /** Host and OpenXR timestamps for the same controller-pose sample. */
    time_point   sample_time{};
    std::int64_t xr_sample_time{0};

    quest_hand_controller left;
    quest_hand_controller right;
};

} // namespace ILLIXR::data_format
