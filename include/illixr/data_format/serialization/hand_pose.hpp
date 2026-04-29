#pragma once

#include "illixr/data_format/poses/hand_pose.hpp"
#include "illixr/data_format/serialization/pose_base.hpp"

namespace boost::serialization {

#ifdef USING_OPENXR

template<class Archive>
void serialize(Archive& ar, HAND_JOINT_TYPE& joint, const unsigned int version) {
    (void) version;
    ar & joint.relation;
    ar & joint.radius;
    // SER_LOG_BOTH(ar, "  hand_joint: radius=%f", joint.radius);
}

template<class Archive>
void serialize(Archive& ar, HAND_JOINT_SET& hand, const unsigned int version) {
    (void) version;
    // SER_LOG_BOTH(ar, "  hand_joint_set: serializing %zu joints", (size_t) HAND_JOINT_COUNT);
    for (size_t i = 0; i < HAND_JOINT_COUNT; ++i) {
    #ifdef ENABLE_MONADO
        ar & hand.values.hand_joint_set_default[i];
    #else
        ar & hand.joints[i];
    #endif
    }

    ar & hand.is_active;
    // SER_LOG_BOTH(ar, "  hand_joint_set: is_active=%d", static_cast<int>(hand.is_active));
}

template<class Archive>
void serialize(Archive& ar, ILLIXR::data_format::pose::hand_joint_poses_pair& data, const unsigned int version) {
    (void) version;

    ar& boost::serialization::base_object<ILLIXR::switchboard::event>(data);
    ar & data.hands[ILLIXR::data_format::pose::LEFT];
    ar & data.hands[ILLIXR::data_format::pose::RIGHT];
    ar & data.sensor_time;
    // SER_LOG_BOTH(ar, "  hand_joint_poses_pair: left_active=%d right_active=%d sensor_time=%lld",
    //              static_cast<int>(data.hands[ILLIXR::data_format::pose::LEFT].is_active),
    //              static_cast<int>(data.hands[ILLIXR::data_format::pose::RIGHT].is_active),
    //              static_cast<long long>(data.sensor_time.time_since_epoch().count()));
}
#endif
} // namespace boost::serialization

#ifdef USING_OPENXR
BOOST_CLASS_EXPORT_KEY(ILLIXR::data_format::pose::hand_joint_poses_pair)
#endif