#pragma once
#ifdef USING_OPENXR
#include "illixr/data_format/poses/pose_base.hpp"
#include "illixr/switchboard.hpp"

#include <map>

#ifdef ENABLE_MONADO
#define INTERACTION_POSE_TYPE xrt_pose
#else
#define INTERACTION_POSE_TYPE XrPosef
#endif
namespace ILLIXR::data_format::pose {

/**
 * @brief Enumeration of the interaction pose types provided by XR_EXT_hand_interaction.
 *
 * Each value corresponds to a distinct action-space pose that the OpenXR runtime
 * derives from the tracked hand skeleton.  Every pose type except @c POKE has a
 * corresponding scalar value and readiness flag; these are carried directly on
 * @c hand_interaction_pose_data alongside the pose itself:
 *
 * | Type  | Pose path             | Value path                    | Ready path                        |
 * |-------|-----------------------|-------------------------------|-----------------------------------|
 * | AIM   | /input/aim/pose       | /input/aim_activate_ext/value | /input/aim_activate_ext/ready_ext |
 * | GRIP  | /input/grip/pose      | /input/grasp_ext/value        | /input/grasp_ext/ready_ext        |
 * | PINCH | /input/pinch_ext/pose | /input/pinch_ext/value        | /input/pinch_ext/ready_ext        |
 * | POKE  | /input/poke_ext/pose  | —                             | —                                 |
 */
enum interaction_pose_type : int {
    AIM   = 0, //!< Hand aim-ray pose     (/input/aim/pose)
    GRIP  = 1, //!< Hand grip pose         (/input/grip/pose)
    PINCH = 2, //!< Pinch interaction pose (/input/pinch_ext/pose)
    POKE  = 3, //!< Poke interaction pose  (/input/poke_ext/pose)
};

/** @brief Total number of interaction pose types. */
constexpr int NUM_INTERACTION_POSES = 4;

/**
 * @brief Pose, gesture-strength value, and readiness flag for a single hand interaction action space.
 *
 * Extends @c pose_base with the two scalar bindings that accompany each interaction
 * pose in XR_EXT_hand_interaction.  The meaning of @c value and @c ready depends on
 * which @c interaction_pose_type this struct represents:
 *
 * | Type  | @c value semantics              | @c ready semantics                    |
 * |-------|---------------------------------|---------------------------------------|
 * | AIM   | /input/aim_activate_ext/value   | /input/aim_activate_ext/ready_ext     |
 * | GRIP  | /input/grasp_ext/value          | /input/grasp_ext/ready_ext            |
 * | PINCH | /input/pinch_ext/value          | /input/pinch_ext/ready_ext            |
 * | POKE  | unused — always 0               | unused — always false                 |
 *
 * @c value is in [0, 1], where 0 means the gesture is not performed and 1 means it
 * is fully activated.
 *
 * @c ready indicates that the runtime considers the hand to be in a position where
 * the gesture could be meaningfully performed (e.g. the index finger is extended and
 * the hand is visible).  An interaction should only be triggered when @c ready is
 * true and @c value crosses the application's chosen threshold.
 */
struct hand_interaction_pose : xrt_space_relation {
    float value; //!< Gesture-strength scalar in [0, 1]; see struct documentation for per-type semantics
    bool  ready; //!< Whether the runtime considers the gesture activatable; see struct documentation
    int64_t predicted_time;

    /**
     * @brief Default constructor. Pose is invalid; value is 0; ready is false.
     */
    hand_interaction_pose()
        : xrt_space_relation{}
        , value{0.f}
        , ready{false}
        , predicted_time{0} { }

    /**
     * @brief Construct from explicit components.
     * @param position_    Translation of the interaction action-space origin
     * @param orientation_ Rotation of the interaction frame (must be unit quaternion)
     * @param value_       Gesture-strength scalar in [0, 1], defaults to 0
     * @param ready_       Whether the gesture is activatable, defaults to false
     */
    [[maybe_unused]]explicit hand_interaction_pose(INTERACTION_POSE_TYPE& in_pose,
                                                   float value_ = 0.f, bool ready_ = false,
                                                   int64_t p_time = 0)
        : xrt_space_relation{}
        , value{value_}
        , ready{ready_}
        , predicted_time{p_time} {
        pose = in_pose;
    }


#ifndef ENABLE_MONADO

    [[maybe_unused]]explicit hand_interaction_pose(XrSpaceLocation& location,
                                                   float value_ = 0.f, bool ready_ = false,
                                                   XrTime p_time = 0)
            : xrt_space_relation{}
            , value{value_}
            , ready{ready_}
            , predicted_time{p_time} {
        pose = location.pose;
        set_flags(location.locationFlags);
    }

    void update(XrSpaceLocation location, float val, bool rdy, XrTime p_time) {
        pose = location.pose;
        value = val;
        ready = rdy;
        set_flags(location.locationFlags);
        predicted_time = p_time;
    }
#endif
    [[maybe_unused]]bool valid() const {
        return (relation_flags & XRT_SPACE_RELATION_POSITION_VALID_BIT) != 0u &&
                (relation_flags & XRT_SPACE_RELATION_ORIENTATION_VALID_BIT) != 0u;
    }
};

/**
 * @brief All interaction poses for one hand.
 *
 * Stores one @c hand_interaction_pose_data for each @c interaction_pose_type.
 * The gesture-strength value and readiness flag for each pose are carried on the
 * @c hand_interaction_pose_data itself rather than at this level, since they are
 * semantically bound to their corresponding pose type.
 */
struct hand_interaction_poses {
    std::map<interaction_pose_type, hand_interaction_pose> poses;        //!< Interaction poses keyed by type

    /**
     * @brief Default constructor. All poses default-constructed; hand is not tracked.
     */
    hand_interaction_poses()
        : poses{
            {AIM,   hand_interaction_pose{}},
            {GRIP,  hand_interaction_pose{}},
            {PINCH, hand_interaction_pose{}},
            {POKE,  hand_interaction_pose{}},
          } {}
    /**
     * @brief Construct from explicit components.
     * @param poses_        Map from interaction type to pose data
     */
    explicit hand_interaction_poses(std::map<interaction_pose_type, hand_interaction_pose> poses_)
        : poses{std::move(poses_)} { }

    /**
     * @brief Access an interaction pose by type.
     * @param type The interaction pose type to access
     * @return Reference to the corresponding @c hand_interaction_pose_data
     */
    hand_interaction_pose& operator[](interaction_pose_type type) {
        return poses[type];
    }

    /**
     * @brief Access an interaction pose by type (const overload).
     * @param type The interaction pose type to access
     * @return Const reference to the corresponding @c hand_interaction_pose_data
     */
    const hand_interaction_pose& at(interaction_pose_type type) const {
        return poses.at(type);
    }

    bool is_valid() const {
        return poses.at(AIM).valid() || poses.at(GRIP).valid() || poses.at(PINCH).valid() || poses.at(POKE).valid();
    }
};

/**
 * @brief Interaction poses for both hands, suitable for publication on the switchboard.
 */
struct hand_interaction_poses_pair : public switchboard::event {
    std::map<hand, hand_interaction_poses> hands;       //!< Per-hand interaction poses keyed by @c hand
    time_point                             sensor_time; //!< Timestamp at which the data was captured

    /**
     * @brief Default constructor. Both hands default-constructed; timestamp is default-constructed.
     */
    hand_interaction_poses_pair()
        : hands{{LEFT, hand_interaction_poses{}}, {RIGHT, hand_interaction_poses{}}}
        , sensor_time{time_point{}} { }

    /**
     * @brief Construct from explicit components.
     * @param hands_       Per-hand interaction poses
     * @param sensor_time_ Timestamp at which the data was captured
     */
    hand_interaction_poses_pair(std::map<hand, hand_interaction_poses> hands_, time_point sensor_time_)
        : hands{std::move(hands_)}
        , sensor_time{sensor_time_} { }

    bool is_valid() const {
        return hands.at(LEFT).is_valid() || hands.at(RIGHT).is_valid();
    }

    hand_interaction_poses& operator[](hand h) {
        return hands[h];
    }

};

} // namespace ILLIXR::data_format
#endif
