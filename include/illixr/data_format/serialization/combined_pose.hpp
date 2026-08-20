#pragma once

#include "illixr/data_format/poses/combined_pose.hpp"
#include "illixr/data_format/serialization/hand_interaction_pose.hpp"
#include "illixr/data_format/serialization/hand_pose.hpp"
#include "illixr/data_format/serialization/head_pose.hpp"
#include "illixr/data_format/serialization/palm_pose.hpp"

namespace boost::serialization {

#ifdef USING_OPENXR

template<class Archive>
void serialize(Archive& ar, ILLIXR::data_format::pose::combined_pose& data, const unsigned int version) {
    (void) version;
    ar& boost::serialization::base_object<ILLIXR::switchboard::event>(data);
    ar & data.head_pose;
    ar & data.hand_poses;
    ar & data.palm_poses;
    ar & data.hand_interactions;
    ar & data.valid_data;
    ar & data.id;
    ar & data.pose_xr_time_ns;
    ar & data.xr_to_monotonic_offset_ns;
    ar & data.monotonic_to_system_offset_ns;
    ar & data.smoothed_clock_offset_ns;
    ar & data.smoothed_rtt_ns;
}
#endif

} // namespace boost::serialization

#ifdef USING_OPENXR
BOOST_CLASS_EXPORT_KEY(ILLIXR::data_format::pose::combined_pose)

#endif