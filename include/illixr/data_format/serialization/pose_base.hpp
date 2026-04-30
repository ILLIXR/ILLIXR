#pragma once

#include "misc.hpp"

namespace boost::serialization {

#ifdef USING_OPENXR
template<class Archive>
void serialize(Archive& ar, POSE_BASE_TYPE& xrt, const unsigned int version) {
    (void) version;
    ar & xrt.pose;
    ar & xrt.linear_velocity;
    ar & xrt.angular_velocity;
    ar & xrt.relation_flags;
    // SER_LOG_BOTH(ar,
    //              "  xrt_space_relation flags=0x%08x "
    //              "pos=(%f,%f,%f) ori=(%f,%f,%f,%f) "
    //              "lin=(%f,%f,%f) ang=(%f,%f,%f)",
    //              static_cast<uint32_t>(xrt.relation_flags), xrt.pose.position.x, xrt.pose.position.y, xrt.pose.position.z,
    //              xrt.pose.orientation.x, xrt.pose.orientation.y, xrt.pose.orientation.z, xrt.pose.orientation.w,
    //              xrt.linear_velocity.x, xrt.linear_velocity.y, xrt.linear_velocity.z, xrt.angular_velocity.x,
    //              xrt.angular_velocity.y, xrt.angular_velocity.z);
}
#else
template<class Archive>
void serialize(Archive& ar, ILLIXR::data_format::pose::pose_base& pose, const unsigned int version) {
    (void) version;
    ar& boost::serialization::make_array(pose.position.derived().data(), pose.position.size());
    ar& boost::serialization::make_array(pose.orientation.coeffs().data(), pose.orientation.coeffs().size());
    ar & pose.confidence;
    ar & pose.valid;
}

#endif

} // namespace boost::serialization