#pragma once
#ifdef USING_OPENXR
#    include "illixr/data_format/poses/pose_base.hpp"
#    include "illixr/switchboard.hpp"

#    include <array>
#    include <cstdint>
#    include <map>
#    include <utility>

#    ifdef ENABLE_MONADO
#        define HAND_JOINT_COUNT XRT_HAND_JOINT_COUNT
#    else
#        define HAND_JOINT_COUNT XR_HAND_JOINT_COUNT_EXT
#        include "openxr_defines.hpp"
#    endif

namespace ILLIXR::data_format::pose {

/**
 * @brief Enumeration of the 26 hand joints tracked by XR_EXT_hand_tracking.
 *
 * The values mirror XrHandJointEXT so that they can be used as direct indices
 * into XrHandJointLocationEXT arrays without a translation step.
 */
enum joint : int {
    PALM [[maybe_unused]]                = 0,
    WRIST [[maybe_unused]]               = 1,
    THUMB_METACARPAL [[maybe_unused]]    = 2,
    THUMB_PROXIMAL [[maybe_unused]]      = 3,
    THUMB_DISTAL [[maybe_unused]]        = 4,
    THUMB_TIP [[maybe_unused]]           = 5,
    INDEX_METACARPAL [[maybe_unused]]    = 6,
    INDEX_PROXIMAL [[maybe_unused]]      = 7,
    INDEX_INTERMEDIATE [[maybe_unused]]  = 8,
    INDEX_DISTAL [[maybe_unused]]        = 9,
    INDEX_TIP [[maybe_unused]]           = 10,
    MIDDLE_METACARPAL [[maybe_unused]]   = 11,
    MIDDLE_PROXIMAL [[maybe_unused]]     = 12,
    MIDDLE_INTERMEDIATE [[maybe_unused]] = 13,
    MIDDLE_DISTAL [[maybe_unused]]       = 14,
    MIDDLE_TIP [[maybe_unused]]          = 15,
    RING_METACARPAL [[maybe_unused]]     = 16,
    RING_PROXIMAL [[maybe_unused]]       = 17,
    RING_INTERMEDIATE [[maybe_unused]]   = 18,
    RING_DISTAL [[maybe_unused]]         = 19,
    RING_TIP [[maybe_unused]]            = 20,
    LITTLE_METACARPAL [[maybe_unused]]   = 21,
    LITTLE_PROXIMAL [[maybe_unused]]     = 22,
    LITTLE_INTERMEDIATE [[maybe_unused]] = 23,
    LITTLE_DISTAL [[maybe_unused]]       = 24,
    LITTLE_TIP [[maybe_unused]]          = 25
};

#    ifdef ENABLE_MONADO
/**
 * @brief Pose and velocity data for a hand joint
 *
 * Using @c xrt_hand_joint_value to be Monado compatable
 */
typedef xrt_hand_joint_value hand_joint_pose;

#    else
/**
 * @brief Full 6-DOF pose for a single hand joint.
 *
 * Extends @c pose_base with per-joint @c joint_location_flags, a linear velocity,
 * and a joint-sphere radius.
 *
 * @c pose_base::valid is set to @c true when both ORIENTATION_VALID and POSITION_VALID
 * are set in @c location_flags, providing a single "is this usable?" check for
 * consumers that do not need the finer-grained distinction.  Consumers that do need
 * it (e.g. physics that should only apply velocity when POSITION_TRACKED is set)
 * should read @c location_flags directly.
 */
struct hand_joint_pose {
    xrt_space_relation relation; //!< The pose, velocity, and flag data for the joint
    float              radius;   //!< The radius of the joint

    hand_joint_pose()
        : radius{0.} { }

    /**
     * Constructor using an xrt_space_relation and radius
     * @param rel
     * @param r
     */
    hand_joint_pose(xrt_space_relation rel, float r)
        : relation{std::move(rel)}
        , radius{r} { }

    /**
     * Constructor using an OpenXR based hand joint and velocity
     * @param pose The pose for the joint
     * @param vel The velocity data for the joint
     */
    hand_joint_pose(XrHandJointLocationEXT pose, XrHandJointVelocityEXT vel) {
        radius                    = pose.radius;
        relation.pose             = pose.pose;
        relation.linear_velocity  = vel.linearVelocity;
        relation.angular_velocity = vel.angularVelocity;
        relation.set_flags(pose.locationFlags, vel.velocityFlags);
    }
};
#    endif

/**
 * @brief All joint poses for one hand, along with hand-level metadata.
 *
 * Wraps an array of @c hand_joint_pose (one per @c joint) and records whether
 * the hand as a whole is currently being tracked.
 */
#    ifdef ENABLE_MONADO
/**
 * @brief Pose and velocity data for all points in a hand
 *
 * Using @c xrt_hand_joint_set to be Monado compatable
 */
typedef xrt_hand_joint_set hand_joint_poses;

#    else

struct hand_joint_poses {
    std::array<hand_joint_pose, XR_HAND_JOINT_COUNT_EXT> joints; //!< Per-joint pose data indexed by @c joint
    bool is_active = false; //!< Overall tracking confidence (0.0 = no confidence, 1.0 = full confidence)

    /**
     * @brief Default constructor. All joints are default-constructed; hand is not tracked.
     */
    hand_joint_poses()
        : joints{}
        , is_active{false} { }

    /**
     * Update an existing instance with new information
     * @param pose The new poses
     * @param velocity The new velocity information
     */
    void update(std::array<XrHandJointLocationEXT, XR_HAND_JOINT_COUNT_EXT>& pose,
                std::array<XrHandJointVelocityEXT, XR_HAND_JOINT_COUNT_EXT>& velocity) {
        uint8_t tracked_count = 0;
        for (uint32_t j = 0; j < XR_HAND_JOINT_COUNT_EXT; j++) {
            joints[j] = {pose[j], velocity[j]};
            if (pose[j].locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT &&
                pose[j].locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)
                tracked_count++;
        }
        is_active = (static_cast<float>(tracked_count) / static_cast<float>(XR_HAND_JOINT_COUNT_EXT)) > 0.;
    }

    /**
     * Constructor using an array of poses and velocities
     * @param pose Array of poses (one for each joint)
     * @param velocity Array of velocities (one for each joint)
     */
    hand_joint_poses(std::array<XrHandJointLocationEXT, XR_HAND_JOINT_COUNT_EXT>& pose,
                     std::array<XrHandJointVelocityEXT, XR_HAND_JOINT_COUNT_EXT>& velocity) {
        update(pose, velocity);
    }

    /**
     * @brief Access a joint pose by its @c joint identifier.
     * @param j The joint to access
     * @return Reference to the corresponding @c hand_joint_pose
     */
    hand_joint_pose& operator[](joint j) {
        return joints[static_cast<int>(j)];
    }

    /**
     * @brief Access a joint pose by its @c joint identifier (const overload).
     * @param j The joint to access
     * @return Const reference to the corresponding @c hand_joint_pose
     */
    const hand_joint_pose& operator[](joint j) const {
        return joints[static_cast<int>(j)];
    }
};
#    endif
/**
 * @brief Joint poses for both hands, suitable for publication on the switchboard.
 *
 * Acts as the primary switchboard topic for hand-tracking pose data.  Each eye
 * of the map holds a @c hand_joint_poses for the corresponding hand.
 */
struct hand_joint_poses_pair : public switchboard::event {
    std::map<side, hand_joint_poses> hands;       //!< Per-hand joint poses keyed by @c hand
    time_point                       sensor_time; //!< Timestamp at which the data was captured

    /**
     * @brief Default constructor. Map is empty; timestamp is default-constructed.
     */
    hand_joint_poses_pair()
        : hands{{LEFT, hand_joint_poses{}}, {RIGHT, hand_joint_poses{}}}
        , sensor_time{time_point{}} { }

    /**
     * @brief Construct from explicit components.
     * @param hands_       Per-hand joint poses
     * @param sensor_time_ Timestamp at which the data was captured
     */
    [[maybe_unused]] hand_joint_poses_pair(std::map<side, hand_joint_poses> hands_, time_point sensor_time_)
        : hands{std::move(hands_)}
        , sensor_time{sensor_time_} { }

    /**
     * Returns whether there is any data for either hand
     * @return True if there is any data for either hand, False otherwise.
     */
    [[nodiscard]] [[maybe_unused]] bool has_hands() const {
        return hands.at(LEFT).is_active || hands.at(RIGHT).is_active;
    }

    /**
     * Get the hand joint data for the specified hand
     * @param h The hand (LEFT or RIGHT) to obtain the joint data for
     * @return The requested hand joint data
     */
    hand_joint_poses& operator[](side h) {
        return hands[h];
    }
};

} // namespace ILLIXR::data_format::pose
#endif
