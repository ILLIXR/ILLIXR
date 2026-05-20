#pragma once

#include "illixr/relative_clock.hpp"
#include "illixr/switchboard.hpp"

#include <boost/serialization/binary_object.hpp>
#include <boost/serialization/export.hpp>

#ifdef USING_OPENXR
#  ifdef ENABLE_MONADO
#    include "xrt/xrt_defines.h"
#    define THREE_VECTOR_TYPE xrt_vec3
#    define QUATERNION_TYPE   xrt_quat
#    define POSE_BASE_TYPE    xrt_space_relation
#    define HAND_JOINT_SET    xrt_hand_joint_set
#    define HAND_JOINT_TYPE   xrt_hand_joint_value
#    define POSE_DATA_TYPE    xrt_pose
#  else
#    include <openxr/openxr.h>
#    define THREE_VECTOR_TYPE XrVector3f
#    define QUATERNION_TYPE   XrQuaternionf
#    define POSE_BASE_TYPE    ILLIXR::data_format::pose::xrt_space_relation
#    define HAND_JOINT_SET    ILLIXR::data_format::pose::hand_joint_poses
#    define HAND_JOINT_TYPE   ILLIXR::data_format::pose::hand_joint_pose
#    define POSE_DATA_TYPE    XrPosef
#  endif
#  include <boost/serialization/array.hpp>
#  include <boost/serialization/map.hpp>
#endif

namespace boost::serialization {
template<class Archive>
[[maybe_unused]] void serialize(Archive& ar, ILLIXR::time_point& tp, const unsigned int version) {
    (void) version;
    if constexpr (Archive::is_saving::value) {
        // Save: convert time_point to int64_t nanoseconds since epoch
        int64_t ns_since_epoch = std::chrono::duration_cast<std::chrono::nanoseconds>(tp.time_since_epoch()).count();
        ar & ns_since_epoch;
    } else {
        // Load: convert int64_t nanoseconds back to time_point
        int64_t ns_since_epoch;
        ar & ns_since_epoch;
        tp = ILLIXR::time_point(std::chrono::nanoseconds(ns_since_epoch));
    }
}
#ifdef USING_OPENXR
template<class Archive>
void serialize(Archive& ar, QUATERNION_TYPE& quat, const unsigned int version) {
    (void) version;
    ar & quat.x;
    ar & quat.y;
    ar & quat.z;
    ar & quat.w;
}

template<class Archive>
void serialize(Archive& ar, THREE_VECTOR_TYPE& position, const unsigned int version) {
    (void) version;
    ar & position.x;
    ar & position.y;
    ar & position.z;
}

template<class Archive>
void serialize(Archive& ar, POSE_DATA_TYPE& pose, const unsigned int version) {
    (void) version;
    ar & pose.position;
    ar & pose.orientation;
}

#endif
} // namespace boost::serialization

BOOST_CLASS_EXPORT_KEY(ILLIXR::switchboard::event)
