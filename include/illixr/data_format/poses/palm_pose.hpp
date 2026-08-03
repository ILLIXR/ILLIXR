#pragma once
#ifdef USING_OPENXR
    #include "illixr/data_format/poses/pose_base.hpp"
    #include "illixr/switchboard.hpp"

    #include <map>

namespace ILLIXR::data_format::pose {

/**
 * @brief Palm pose and velocity information
 */
struct palm_pose : xrt_space_relation {
    /**
     * @brief Returns whether or not there are any valid bit in the internal flags.
     */
    bool is_valid() const {
        return relation_flags > 0;
    }
    #ifndef ENABLE_MONADO
    /**
     * @brief Update the internal data memebers
     *
     * @param location The new palm location information
     * @param velocity The new paml velocity information
     */
    void update(XrSpaceLocation& location, XrSpaceVelocity& velocity) {
        pose             = location.pose;
        linear_velocity  = velocity.linearVelocity;
        angular_velocity = velocity.angularVelocity;
        set_flags(location.locationFlags, velocity.velocityFlags);
    }
    #endif
};

/**
 * @brief Palm poses for both hands, suitable for publication on the switchboard.
 *
 * Acts as the primary switchboard topic for palm pose data derived from
 * XR_EXT_palm_pose or the PALM joint of XR_EXT_hand_tracking.
 */
struct palm_poses_pair : public switchboard::event {
    std::map<side, palm_pose> hands;       //!< Per-hand palm pose keyed by @c hand
    time_point                sensor_time; //!< Timestamp at which the data was captured

    /**
     * @brief Default constructor. Both hands default-constructed; timestamp is default-constructed.
     */
    palm_poses_pair()
        : hands{{LEFT, palm_pose{}}, {RIGHT, palm_pose{}}}
        , sensor_time{time_point{}} { }

    /**
     * @brief Construct from explicit components.
     * @param hands_       Per-hand palm poses
     * @param sensor_time_ Timestamp at which the data was captured
     */
    palm_poses_pair(std::map<side, palm_pose> hands_, time_point sensor_time_)
        : hands{std::move(hands_)}
        , sensor_time{sensor_time_} { }

    bool is_valid() const {
        return hands.at(LEFT).relation_flags != XRT_SPACE_RELATION_BITMASK_NONE ||
            hands.at(RIGHT).relation_flags != XRT_SPACE_RELATION_BITMASK_NONE;
    }

    palm_pose& operator[](side h) {
        return hands[h];
    }
};

} // namespace ILLIXR::data_format::pose
#endif
