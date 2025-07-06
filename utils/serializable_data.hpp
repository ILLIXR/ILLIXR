#pragma once

#include "illixr/data_format/frame.hpp"

#include <boost/serialization/binary_object.hpp>

namespace boost::serialization {
template<class Archive>
[[maybe_unused]] void serialize(Archive& ar, ILLIXR::time_point& tp, const unsigned int version) {
    (void) version;
    ar& boost::serialization::make_binary_object(&tp.time_since_epoch_, sizeof(tp.time_since_epoch_));
}

template<class Archive>
void serialize(Archive& ar, ILLIXR::data_format::pose_type& pose, const unsigned int version) {
    (void) version;
    ar& boost::serialization::base_object<ILLIXR::switchboard::event>(pose);
    ar& pose.cam_time;
    ar& pose.imu_time;
    ar& boost::serialization::make_array(pose.position.derived().data(), pose.position.size());
    ar& boost::serialization::make_array(pose.orientation.coeffs().data(), pose.orientation.coeffs().size());
}

template<class Archive>
[[maybe_unused]] void serialize(Archive& ar, ILLIXR::data_format::fast_pose_type& pose, const unsigned int version) {
    (void) version;
    ar& boost::serialization::base_object<ILLIXR::switchboard::event>(pose);
    ar & pose.pose;
    ar & pose.predict_computed_time;
    ar & pose.predict_target_time;
}
} // namespace boost::serialization

BOOST_CLASS_EXPORT_KEY(ILLIXR::switchboard::event)
BOOST_CLASS_EXPORT_KEY(ILLIXR::data_format::compressed_frame)
BOOST_CLASS_EXPORT_KEY(ILLIXR::data_format::pose_type)
BOOST_CLASS_EXPORT_KEY(ILLIXR::data_format::fast_pose_type)
