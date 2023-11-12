//
// Created by steven on 11/5/23.
//

#ifndef ILLIXR_SERIALIZABLE_DATA_HPP
#define ILLIXR_SERIALIZABLE_DATA_HPP

#include "data_format.hpp"
#include "switchboard.hpp"

#include <boost/serialization/binary_object.hpp>
#include <boost/serialization/export.hpp>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
}

namespace ILLIXR {
struct compressed_frame : public switchboard::event {
    AVPacket* left;
    AVPacket* right;
    fast_pose_type pose;

    friend class boost::serialization::access;

    template<class Archive>
    static void save_packet(Archive& ar, AVPacket* pkt) {
        ar << pkt->size;
        ar << boost::serialization::make_array(pkt->data, pkt->size);
        ar << pkt->pts;
        ar << pkt->dts;
        ar << pkt->stream_index;
        ar << pkt->flags;
        ar << pkt->duration;
        ar << pkt->pos;
        ar << pkt->time_base.num;
        ar << pkt->time_base.den;
        ar << pkt->side_data_elems;
        for (int i = 0; i < pkt->side_data_elems; i++) {
            ar << pkt->side_data[i].type;
            ar << pkt->side_data[i].size;
            ar << boost::serialization::make_array(pkt->side_data[i].data, pkt->side_data[i].size);
        }
    }

    template<class Archive>
    static void load_packet(Archive& ar, AVPacket* pkt) {
        ar >> pkt->size;
        pkt->buf  = av_buffer_alloc(pkt->size);
        pkt->data = pkt->buf->data;
        ar >> boost::serialization::make_array(pkt->data, pkt->size);
        ar >> pkt->pts;
        ar >> pkt->dts;
        ar >> pkt->stream_index;
        ar >> pkt->flags;
        ar >> pkt->duration;
        ar >> pkt->pos;
        ar >> pkt->time_base.num;
        ar >> pkt->time_base.den;
        ar >> pkt->side_data_elems;
        pkt->side_data = (AVPacketSideData*) malloc(sizeof(AVPacketSideData) * pkt->side_data_elems);
        for (int i = 0; i < pkt->side_data_elems; i++) {
            ar >> pkt->side_data[i].type;
            ar >> pkt->side_data[i].size;
            pkt->side_data[i].data = (uint8_t*) malloc(pkt->side_data[i].size);
            ar >> boost::serialization::make_array(pkt->side_data[i].data, pkt->side_data[i].size);
        }
    }

    template<class Archive>
    void save(Archive& ar, const unsigned int version) const {
        ar << boost::serialization::base_object<switchboard::event>(*this);
        save_packet(ar, left);
        save_packet(ar, right);
        ar << pose;
    }

    template<class Archive>
    void load(Archive& ar, const unsigned int version) {
        ar >> boost::serialization::base_object<switchboard::event>(*this);
        left = av_packet_alloc();
        load_packet(ar, left);
        right = av_packet_alloc();
        load_packet(ar, right);
        ar >> pose;
    }

    BOOST_SERIALIZATION_SPLIT_MEMBER()

    compressed_frame() = default;

    compressed_frame(AVPacket* left, AVPacket* right, const fast_pose_type& pose)
        : left(left)
        , right(right)
        , pose(pose) { }
};
} // namespace ILLIXR

namespace boost::serialization {
template<class Archive>
void serialize(Archive& ar, ILLIXR::time_point& tp, const unsigned int version) {
    ar& boost::serialization::make_binary_object(&tp._m_time_since_epoch, sizeof(tp._m_time_since_epoch));
}

template<class Archive>
void serialize(Archive& ar, ILLIXR::pose_type& pose, const unsigned int version) {
    ar& boost::serialization::base_object<ILLIXR::switchboard::event>(pose);
    ar & pose.sensor_time;
    ar& boost::serialization::make_array(pose.position.derived().data(), pose.position.size());
    ar& boost::serialization::make_array(pose.orientation.coeffs().data(), pose.orientation.coeffs().size());
}

template<class Archive>
void serialize(Archive& ar, ILLIXR::fast_pose_type& pose, const unsigned int version) {
    ar& boost::serialization::base_object<ILLIXR::switchboard::event>(pose);
    ar & pose.pose;
    ar & pose.predict_computed_time;
    ar & pose.predict_target_time;
}
} // namespace boost::serialization

BOOST_CLASS_EXPORT_KEY(ILLIXR::switchboard::event)
BOOST_CLASS_EXPORT_KEY(ILLIXR::compressed_frame)
BOOST_CLASS_EXPORT_KEY(ILLIXR::pose_type)
BOOST_CLASS_EXPORT_KEY(ILLIXR::fast_pose_type)
#endif // ILLIXR_SERIALIZABLE_DATA_HPP
