#pragma once

#include "illixr/data_format/hmd_config.hpp"

#include <boost/serialization/binary_object.hpp>
#include <boost/serialization/export.hpp>

namespace boost::serialization {

template<class Archive>
void serialize(Archive& ar, hmd_config& data, const unsigned int version) {
    (void) version;
    ar & data.recommended_image_width;
    ar & data.recommended_image_height;
    ar & data.fov_angle_left[0];
    ar & data.fov_angle_left[1];
    ar & data.fov_angle_right[0];
    ar & data.fov_angle_right[1];
    ar & data.fov_angle_up[0];
    ar & data.fov_angle_up[1];
    ar & data.fov_angle_down[0];
    ar & data.fov_angle_down[1];
}

template<class Archive>
void serialize(Archive& ar, ILLIXR::data_format::hmd_config_data& data, const unsigned int version) {
    (void) version;
    ar& boost::serialization::base_object<ILLIXR::switchboard::event>(data);
    ar & data.config;
}
} // namespace boost::serialization

BOOST_CLASS_EXPORT_KEY(ILLIXR::data_format::hmd_config_data)
