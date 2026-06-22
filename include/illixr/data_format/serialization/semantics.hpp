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
void serialize(Archive& ar, ILLIXR::data_format::semantic_data& data, const unsigned int version) {
    (void) version;

    ar& boost::serialization::base_object<ILLIXR::switchboard::event>(data);
    ar & data.image;
    ar & data.intrinsics;
    ar & data.depth;
    ar & data.depth_near_z;
    ar & data.depth_intrinsics;
    ar & boost::serialization::make_array(data.rgb_camera_pose,  16);
    ar & boost::serialization::make_array(data.depth_pose,       16);
    ar & data.max_depth;
    ar & data.frame_number;
    ar & data.rgb_timestamp_ns;
    ar & data.depth_timestamp_ns;
}

template<class Archive>
void serialize(Archive& ar, ILLIXR::data_format::voice_query& data, const unsigned int) {
    ar & boost::serialization::base_object<ILLIXR::switchboard::event>(data);
    ar & data.query_id;
    ar & data.pcm_data;
    ar & data.similarity_threshold;
    ar & data.min_match_similarity;
}

template<class Archive>
void serialize(Archive& ar, ILLIXR::data_format::point_cloud& data, const unsigned int) {
    ar & data.points;
    ar & data.centroid;
    ar & data.num_points;
}

template<class Archive>
void serialize(Archive& ar, ILLIXR::data_format::query_response& data, const unsigned int) {
    ar & boost::serialization::base_object<ILLIXR::switchboard::event>(data);
    ar & data.query_id;
    ar & data.point_clouds;    // vector<point_cloud> — uses point_cloud serializer above
    ar & data.colors;
    ar & data.num_point_clouds;
    ar & data.server_query_processing;
    ar & data.text_query;
}
} // namespace boost::serialization

BOOST_CLASS_EXPORT_KEY(ILLIXR::data_format::semantic_data)
BOOST_CLASS_EXPORT_KEY(ILLIXR::data_format::voice_query)
BOOST_CLASS_EXPORT_KEY(ILLIXR::data_format::query_response)
