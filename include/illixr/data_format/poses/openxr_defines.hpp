#pragma once

#ifdef USING_OPENXR
#ifdef ENABLE_MONADO
#include <xrt/xrt_defines.h>
#else
#include <openxr/openxr.h>
#include <stdint.h>

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
    XRT_SPACE_RELATION_BITMASK_ALL = (uint32_t)XRT_SPACE_RELATION_ORIENTATION_VALID_BIT |      //
                                     (uint32_t)XRT_SPACE_RELATION_POSITION_VALID_BIT |         //
                                     (uint32_t)XRT_SPACE_RELATION_LINEAR_VELOCITY_VALID_BIT |  //
                                     (uint32_t)XRT_SPACE_RELATION_ANGULAR_VELOCITY_VALID_BIT | //
                                     (uint32_t)XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT |    //
                                     (uint32_t)XRT_SPACE_RELATION_POSITION_TRACKED_BIT,
    XRT_SPACE_RELATION_BITMASK_NONE = 0,
};

/*!
 * A relation with two spaces, includes velocity and acceleration.
 *
 * @see xrt_quat
 * @see xrt_vec3
 * @see xrt_pose
 * @see xrt_space_relation_flags
 * @ingroup xrt_iface math
 */
struct xrt_space_relation {
    uint32_t relation_flags;
    struct XrPosef pose;
    struct XrVector3f linear_velocity;
    struct XrVector3f angular_velocity;
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
    xrt_space_relation()
        : relation_flags{XRT_SPACE_RELATION_BITMASK_NONE}
        , pose{}
        , linear_velocity{}
        , angular_velocity{} {}

    [[maybe_unused]]xrt_space_relation(XrSpaceLocation location, XrSpaceVelocity velocity)
        : pose{location.pose}
        , linear_velocity{velocity.linearVelocity}
        , angular_velocity{velocity.angularVelocity} {
        set_flags(location.locationFlags, velocity.velocityFlags);
    }

    bool valid() const {
        return (relation_flags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0u &&
               (relation_flags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0u;
    }

};

#endif
#endif
