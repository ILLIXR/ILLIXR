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
