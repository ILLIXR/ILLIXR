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
}

template<class Archive>
void serialize(Archive& ar, HAND_JOINT_SET& hand, const unsigned int version) {
    (void) version;
    for (size_t i = 0; i < HAND_JOINT_COUNT; ++i) {
    #ifdef ENABLE_MONADO
        ar & hand.values.hand_joint_set_default[i];
    #else
        ar & hand.joints[i];
    #endif
    }

    ar & hand.is_active;
}

template<class Archive>
void serialize(Archive& ar, ILLIXR::data_format::pose::hand_joint_poses_pair& data, const unsigned int version) {
    (void) version;

    ar& boost::serialization::base_object<ILLIXR::switchboard::event>(data);
    ar & data.hands[ILLIXR::data_format::pose::LEFT];
    ar & data.hands[ILLIXR::data_format::pose::RIGHT];
    ar & data.sensor_time;
}
#endif
} // namespace boost::serialization

#ifdef USING_OPENXR
BOOST_CLASS_EXPORT_KEY(ILLIXR::data_format::pose::hand_joint_poses_pair)
#endif
