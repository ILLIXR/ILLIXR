#pragma once

#include "illixr/data_format/poses/hand_interaction_pose.hpp"
#include "illixr/data_format/serialization/pose_base.hpp"

namespace boost::serialization {

#ifdef USING_OPENXR
template<class Archive>
void serialize(Archive& ar, ILLIXR::data_format::pose::hand_interaction_pose& data, const unsigned int version) {
    (void) version;
    ar& boost::serialization::base_object<POSE_BASE_TYPE>(data);
    ar & data.value;
    uint8_t ready_byte = data.ready ? 1u : 0u;
    ar & ready_byte;
    if constexpr (Archive::is_loading::value) {
        data.ready = (ready_byte != 0u);
    }
    ar & data.predicted_time;
    // SER_LOG_BOTH(ar, "  hand_interaction_pose: flags=0x%08x value=%f ready=%d predicted_time=%lld",
    //              static_cast<uint32_t>(data.relation_flags), data.value, static_cast<int>(data.ready),
    //              static_cast<long long>(data.predicted_time));
}

template<class Archive>
void serialize(Archive& ar, ILLIXR::data_format::pose::hand_interaction_poses& data, const unsigned int version) {
    (void) version;
    // SER_LOG_BOTH(ar, "  hand_interaction_poses: BEGIN map_size=%zu", data.poses.size());
    ar & data.poses[ILLIXR::data_format::pose::AIM];
    ar & data.poses[ILLIXR::data_format::pose::GRIP];
    ar & data.poses[ILLIXR::data_format::pose::PINCH];
    ar & data.poses[ILLIXR::data_format::pose::POKE];
    // SER_LOG_BOTH(ar, "  hand_interaction_poses: END map_size=%zu", data.poses.size());
}

template<class Archive>
void serialize(Archive& ar, ILLIXR::data_format::pose::hand_interaction_poses_pair& data, const unsigned int version) {
    (void) version;
    // SER_LOG_BOTH(ar, "  hand_interaction_poses_pair: BEGIN {}", 0);
    ar& boost::serialization::base_object<ILLIXR::switchboard::event>(data);
    ar & data.hands[ILLIXR::data_format::pose::LEFT];
    ar & data.hands[ILLIXR::data_format::pose::RIGHT];
    ar & data.sensor_time;
    // SER_LOG_BOTH(ar, "  hand_interaction_poses_pair: END sensor_time=%lld",
    //              static_cast<long long>(data.sensor_time.time_since_epoch().count()));
}

#endif

} // namespace boost::serialization

#ifdef USING_OPENXR
BOOST_CLASS_EXPORT_KEY(ILLIXR::data_format::pose::hand_interaction_poses_pair)
#endif