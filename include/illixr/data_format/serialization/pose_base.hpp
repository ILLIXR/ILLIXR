#pragma once

#include "illixr/data_format/poses/pose_base.hpp"
#include "misc.hpp"

#ifdef USING_OPENXR
#  include "openxr.hpp"
#endif

namespace boost::serialization {

#ifdef USING_OPENXR
template<class Archive>
void serialize(Archive& ar, POSE_BASE_TYPE& xrt, const unsigned int version) {
    (void) version;
    ar & xrt.pose;
    ar & xrt.linear_velocity;
    ar & xrt.angular_velocity;
    ar & xrt.relation_flags;
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
