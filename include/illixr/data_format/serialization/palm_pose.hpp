#pragma once

#include "illixr/data_format/poses/palm_pose.hpp"
#include "illixr/data_format/serialization/pose_base.hpp"

namespace boost::serialization { 

#ifdef USING_OPENXR
template<class Archive>
void serialize(Archive& ar, ILLIXR::data_format::pose::palm_pose& data, const unsigned int version) {
    (void) version;
    // Serialize fields directly — do NOT use base_object<> here.
    // On the client palm_pose inherits xrt_space_relation; on the server
    // it IS xrt_space_relation via typedef. Using base_object on the client
    // writes a Boost class-tracking header that the server never reads,
    // causing stream misalignment after the first palm pose.
    ar & data.pose;
    ar & data.linear_velocity;
    ar & data.angular_velocity;
    ar & data.relation_flags;
    // SER_LOG_BOTH(ar, "  palm_pose: flags=0x%08x", static_cast<uint32_t>(data.relation_flags));
}

template<class Archive>
void serialize(Archive& ar, ILLIXR::data_format::pose::palm_poses_pair& data, const unsigned int version) {
    (void) version;

    ar& boost::serialization::base_object<ILLIXR::switchboard::event>(data);
    ar & data.hands[ILLIXR::data_format::pose::LEFT];
    ar & data.hands[ILLIXR::data_format::pose::RIGHT];
    ar & data.sensor_time;
    // SER_LOG_BOTH(ar, "  palm_poses_pair: left_flags=0x%08x right_flags=0x%08x sensor_time=%lld",
    //              static_cast<uint32_t>(data.hands[ILLIXR::data_format::pose::LEFT].relation_flags),
    //              static_cast<uint32_t>(data.hands[ILLIXR::data_format::pose::RIGHT].relation_flags),
    //              static_cast<long long>(data.sensor_time.time_since_epoch().count()));
}

#endif

} // namespace boost::serialization

#ifdef USING_OPENXR
BOOST_CLASS_EXPORT_KEY(ILLIXR::data_format::pose::palm_poses_pair)
#endif