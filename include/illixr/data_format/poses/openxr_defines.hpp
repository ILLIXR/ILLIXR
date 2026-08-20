#pragma once

#ifdef USING_OPENXR
#    ifdef ENABLE_MONADO
#        include <xrt/xrt_defines.h>
#    else
#        include <cstdint>
#        include <openxr/openxr.h>

namespace ILLIXR::data_format::pose {
// From monado xrt_defines.h
enum xrt_space_relation_flags : uint32_t {
    // clang-format off
    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT =          (1u << 0u),
    XRT_SPACE_RELATION_POSITION_VALID_BIT =             (1u << 1u),
    XRT_SPACE_RELATION_LINEAR_VELOCITY_VALID_BIT =      (1u << 2u),
    XRT_SPACE_RELATION_ANGULAR_VELOCITY_VALID_BIT =     (1u << 3u),
    XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT =        (1u << 4u),
    XRT_SPACE_RELATION_POSITION_TRACKED_BIT =           (1u << 5u),
    // clang-format on
    XRT_SPACE_RELATION_BITMASK_ALL [[maybe_unused]] = (uint32_t) XRT_SPACE_RELATION_ORIENTATION_VALID_BIT | //
        (uint32_t) XRT_SPACE_RELATION_POSITION_VALID_BIT |                                                  //
        (uint32_t) XRT_SPACE_RELATION_LINEAR_VELOCITY_VALID_BIT |                                           //
        (uint32_t) XRT_SPACE_RELATION_ANGULAR_VELOCITY_VALID_BIT |                                          //
        (uint32_t) XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT |                                             //
        (uint32_t) XRT_SPACE_RELATION_POSITION_TRACKED_BIT,
    XRT_SPACE_RELATION_BITMASK_NONE = 0,
};

/**
 * A pose with linear and angular velocities and validity flags
 *
 * @see XrPosef
 * @see XrVector3f
 * @see xrt_space_relation_flags
 */
struct xrt_space_relation {
    uint32_t          relation_flags{XRT_SPACE_RELATION_BITMASK_NONE}; //!< validity flags
    struct XrPosef    pose;                                            //!< current pose
    struct XrVector3f linear_velocity;                                 //!< instantaneous linear velocity of the pose
    struct XrVector3f angular_velocity;                                //!< instantaneous angular velocity of the pose

    /**
     * Set the flags to the given value
     * @param location_flags The value to set
     */
    void set_flags(const XrSpaceLocationFlags location_flags) {
        relation_flags = XRT_SPACE_RELATION_BITMASK_NONE;
        if (location_flags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)
            relation_flags |= XRT_SPACE_RELATION_ORIENTATION_VALID_BIT;
        if (location_flags & XR_SPACE_LOCATION_POSITION_VALID_BIT)
            relation_flags |= XRT_SPACE_RELATION_POSITION_VALID_BIT;
        if (location_flags & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT)
            relation_flags |= XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT;
        if (location_flags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT)
            relation_flags |= XRT_SPACE_RELATION_POSITION_TRACKED_BIT;
    }

    /**
     * Set the flags from a combination of location and velocity based flags
     * @param location_flags The location based flags to use
     * @param velocity_flags The velocity based flags to use
     */
    void set_flags(const XrSpaceLocationFlags location_flags, const XrSpaceVelocityFlags velocity_flags) {
        set_flags(location_flags);
        if (location_flags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)
            relation_flags |= XRT_SPACE_RELATION_ORIENTATION_VALID_BIT;
        if (location_flags & XR_SPACE_LOCATION_POSITION_VALID_BIT)
            relation_flags |= XRT_SPACE_RELATION_POSITION_VALID_BIT;
        if (location_flags & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT)
            relation_flags |= XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT;
        if (location_flags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT)
            relation_flags |= XRT_SPACE_RELATION_POSITION_TRACKED_BIT;
        if (velocity_flags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT)
            relation_flags |= XRT_SPACE_RELATION_LINEAR_VELOCITY_VALID_BIT;
        if (velocity_flags & XR_SPACE_VELOCITY_ANGULAR_VALID_BIT)
            relation_flags |= XRT_SPACE_RELATION_ANGULAR_VELOCITY_VALID_BIT;
    }

    /**
     * Default constructor
     */
    xrt_space_relation()
        : relation_flags{XRT_SPACE_RELATION_BITMASK_NONE}
        , pose{}
        , linear_velocity{}
        , angular_velocity{} { }

    /**
     * Constructor
     * @param location The location information to use
     * @param velocity The velocity information to use
     */
    [[maybe_unused]] xrt_space_relation(XrSpaceLocation location, XrSpaceVelocity velocity)
        : pose{location.pose}
        , linear_velocity{velocity.linearVelocity}
        , angular_velocity{velocity.angularVelocity} {
        set_flags(location.locationFlags, velocity.velocityFlags);
    }

    [[nodiscard]] virtual /**
                           * Returns whether the location is considered valid
                           * @return True if both position and orientation are valid, False otherwise.
                           */
        bool
        valid() const {
        return (relation_flags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0u &&
            (relation_flags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0u;
    }
};
} // namespace ILLIXR::data_format::pose
#    endif
#endif
