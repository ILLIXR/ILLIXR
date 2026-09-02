#pragma once

#include "illixr/data_format/quest_controller.hpp"
#include "illixr/data_format/serialization/misc.hpp"

#include <boost/serialization/export.hpp>

namespace boost::serialization {

/** Serialize an Eigen-backed pose as scalar fields followed by tracking flags. */
template<class Archive>
void serialize(Archive& ar, ILLIXR::data_format::quest_controller_pose& pose, const unsigned int) {
    ar & pose.position.x();
    ar & pose.position.y();
    ar & pose.position.z();
    ar & pose.orientation.w();
    ar & pose.orientation.x();
    ar & pose.orientation.y();
    ar & pose.orientation.z();
    ar & pose.active;
    ar & pose.position_valid;
    ar & pose.orientation_valid;
    ar & pose.position_tracked;
    ar & pose.orientation_tracked;
}

/** Serialize one boolean or thresholded-analog controller action. */
template<class Archive>
void serialize(Archive& ar, ILLIXR::data_format::quest_controller_button& button, const unsigned int) {
    ar & button.active;
    ar & button.pressed;
    ar & button.changed_since_last_sync;
    ar & button.value;
    ar & button.last_change_time;
}

/** Serialize one two-dimensional controller action. */
template<class Archive>
void serialize(Archive& ar, ILLIXR::data_format::quest_controller_axis2d& axis, const unsigned int) {
    ar & axis.active;
    ar & axis.changed_since_last_sync;
    ar & axis.value.x();
    ar & axis.value.y();
    ar & axis.last_change_time;
}

/** Serialize every action source associated with one hand. */
template<class Archive>
void serialize(Archive& ar, ILLIXR::data_format::quest_hand_controller& hand, const unsigned int) {
    ar & hand.available;
    ar & hand.interaction_profile;
    ar & hand.grip_pose;
    ar & hand.aim_pose;
    ar & hand.trigger;
    ar & hand.squeeze;
    ar & hand.primary;
    ar & hand.secondary;
    ar & hand.thumbstick_click;
    ar & hand.thumbstick;
}

/** Serialize the complete two-hand switchboard event. */
template<class Archive>
void serialize(Archive& ar, ILLIXR::data_format::quest_controller_input& input, const unsigned int) {
    ar& boost::serialization::base_object<ILLIXR::switchboard::event>(input);
    ar & input.sequence;
    ar & input.sample_time;
    ar & input.xr_sample_time;
    ar & input.left;
    ar & input.right;
}

} // namespace boost::serialization

BOOST_CLASS_EXPORT_KEY(ILLIXR::data_format::quest_controller_input)
