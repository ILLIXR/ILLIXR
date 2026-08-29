#pragma once

#include "illixr/data_format/poses/head_pose.hpp"
#include "illixr/data_format/serialization/pose_base.hpp"
#include "misc.hpp"

#include <boost/serialization/base_object.hpp>

namespace boost::serialization {

#ifndef USING_OPENXR
template<class Archive>
[[maybe_unused]] void serialize(Archive& ar, ILLIXR::data_format::pose::head_pose_data& pose,
                                const unsigned int version) {
    (void) version;
    ar& boost::serialization::base_object<ILLIXR::data_format::pose::pose_base>(pose);
}

template<class Archive>
[[maybe_unused]] void serialize(Archive& ar, ILLIXR::data_format::pose::head_pose_type& pose,
                                const unsigned int version) {
    (void) version;
    ar& boost::serialization::base_object<ILLIXR::switchboard::event>(pose);
    ar& boost::serialization::base_object<ILLIXR::data_format::pose::head_pose_data>(pose);
    ar & pose.sensor_time;
    ar& boost::serialization::make_array(pose.linear_velocity.derived().data(), pose.linear_velocity.size());
    ar& boost::serialization::make_array(pose.angular_velocity.derived().data(), pose.angular_velocity.size());
    ar & pose.linear_velocity_valid;
    ar & pose.angular_velocity_valid;
}
#endif

template<class Archive>
[[maybe_unused]] void serialize(Archive& ar, ILLIXR::data_format::pose::fast_head_pose_type& pose,
                                const unsigned int version) {
    (void) version;
    ar& boost::serialization::base_object<ILLIXR::switchboard::event>(pose);
    ar & pose.pose;
    ar & pose.predict_computed_time;
    ar & pose.predict_target_time;
}

} // namespace boost::serialization

BOOST_CLASS_EXPORT_KEY(ILLIXR::data_format::pose::head_pose_type)
BOOST_CLASS_EXPORT_KEY(ILLIXR::data_format::pose::fast_head_pose_type)
