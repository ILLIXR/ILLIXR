#pragma once

#include "illixr/data_format/poses/head_pose.hpp"
#include "illixr/data_format/serialization/pose_base.hpp"
#include "misc.hpp"

namespace boost::serialization {

template<class Archive>
[[maybe_unused]] void serialize(Archive& ar, ILLIXR::data_format::pose::fast_head_pose_type& pose, const unsigned int version) {
    (void) version;
    ar& boost::serialization::base_object<ILLIXR::switchboard::event>(pose);
    ar & pose.pose;
    ar & pose.predict_computed_time;
    ar & pose.predict_target_time;
}

} // namespace boost::serialization

BOOST_CLASS_EXPORT_KEY(ILLIXR::data_format::pose::head_pose_type)
BOOST_CLASS_EXPORT_KEY(ILLIXR::data_format::pose::fast_head_pose_type)
