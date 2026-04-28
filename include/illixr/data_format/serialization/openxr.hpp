#pragma once

#include "misc.hpp"

#ifdef USING_OPENXR
#ifndef ENABLE_MONADO
#include <openxr/openxr.h>
#else
#include "xrt/xrt_defines.h"
#endif
#endif
namespace boost::serialization {

#ifdef USING_OPENXR
template<class Archive>
void serialize(Archive& ar, QUATERNION_TYPE& quat, const unsigned int version) {
    (void) version;
    ar & quat.x;
    ar & quat.y;
    ar & quat.z;
    ar & quat.w;
    // SER_LOG_BOTH(ar, "  quat=(%f,%f,%f,%f)", quat.x, quat.y, quat.z, quat.w);
}

template<class Archive>
void serialize(Archive& ar, THREE_VECTOR_TYPE& position, const unsigned int version) {
    (void) version;
    ar & position.x;
    ar & position.y;
    ar & position.z;
    // SER_LOG_BOTH(ar, "  vec3=(%f,%f,%f)", position.x, position.y, position.z);
}

template<class Archive>
void serialize(Archive& ar, POSE_DATA_TYPE& pose, const unsigned int version) {
    (void) version;
    ar & pose.position;
    ar & pose.orientation;
    // SER_LOG_BOTH(ar, "  XrPosef pos=(%f,%f,%f) ori=(%f,%f,%f,%f)", pose.position.x, pose.position.y, pose.position.z,
    //              pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w);
}
#endif


}