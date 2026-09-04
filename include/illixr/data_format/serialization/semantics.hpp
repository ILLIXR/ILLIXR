#pragma once

#include "illixr/data_format/semantics.hpp"

#include <boost/serialization/array.hpp>
#include <boost/serialization/base_object.hpp>
#include <boost/serialization/export.hpp>
#include <boost/serialization/vector.hpp>

namespace boost::serialization {

template<class Archive>
void serialize(Archive& ar, ILLIXR::data_format::camera_intrinsics& data, const unsigned int) {
    ar & data.fx;
    ar & data.fy;
    ar & data.cx;
    ar & data.cy;
    ar & data.width;
    ar & data.height;
}

template<class Archive>
void serialize(Archive& ar, ILLIXR::data_format::semantic_frame& data, const unsigned int version) {
    (void) version;

    ar& boost::serialization::base_object<ILLIXR::switchboard::event>(data);
    ar & data.image;
    ar & data.intrinsics;
    ar & data.depth;
    ar & data.depth_near_z;
    ar & data.depth_intrinsics;
    ar& boost::serialization::make_array(data.rgb_camera_pose, 16);
    ar& boost::serialization::make_array(data.depth_pose, 16);
    ar & data.max_depth;
    ar & data.frame_number;
    ar & data.rgb_timestamp_ns;
    ar & data.depth_timestamp_ns;
}
} // namespace boost::serialization

BOOST_CLASS_EXPORT_KEY(ILLIXR::data_format::semantic_frame)
