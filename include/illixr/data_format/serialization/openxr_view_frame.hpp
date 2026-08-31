#pragma once

#include "illixr/data_format/openxr_view_frame.hpp"
#include "illixr/data_format/serialization/misc.hpp"

#include <boost/serialization/export.hpp>

namespace boost::serialization {

template<class Archive>
void serialize(Archive& ar, ILLIXR::data_format::openxr_eye_view& view, const unsigned int) {
    ar & view.position.x();
    ar & view.position.y();
    ar & view.position.z();
    ar & view.orientation.w();
    ar & view.orientation.x();
    ar & view.orientation.y();
    ar & view.orientation.z();
    ar & view.angle_left;
    ar & view.angle_right;
    ar & view.angle_up;
    ar & view.angle_down;
    ar & view.recommended_width;
    ar & view.recommended_height;
    ar & view.pose_valid;
    ar & view.pose_tracked;
}

template<class Archive>
void serialize(Archive& ar, ILLIXR::data_format::openxr_view_frame& frame, const unsigned int) {
    ar& boost::serialization::base_object<ILLIXR::switchboard::event>(frame);
    ar & frame.sequence;
    ar & frame.sample_time;
    ar & frame.xr_sample_time;
    ar & frame.xr_predicted_display_period;
    ar & frame.should_render;
    ar & frame.left;
    ar & frame.right;
}

} // namespace boost::serialization

BOOST_CLASS_EXPORT_KEY(ILLIXR::data_format::openxr_view_frame)
