//
// Created by steven on 11/5/23.
//

#ifndef ILLIXR_SERIALIZABLE_DATA_HPP
#define ILLIXR_SERIALIZABLE_DATA_HPP

#include "cereal/types/polymorphic.hpp"
#include "cereal/archives/binary.hpp"
#include "cereal/access.hpp"

#include "data_format.hpp"
#include "switchboard.hpp"

extern "C" {
#include "libavcodec_illixr/avcodec.h"
#include "libavformat_illixr/avformat.h"
#include "libavutil_illixr/hwcontext.h"
#include "libavutil_illixr/opt.h"
#include "libavutil_illixr/pixdesc.h"
}

namespace ILLIXR {
struct compressed_frame : public switchboard::event {
    AVPacket* left_color;
    AVPacket* right_color;

    bool use_depth;
    AVPacket* left_depth;
    AVPacket* right_depth;
    
    fast_pose_type pose;
    uint64_t sent_time;

    friend class cereal::access;

    template<class Archive>
    static void save_packet(Archive& ar, AVPacket* pkt) {
        ar << pkt->size;
        ar << cereal::binary_data(pkt->data, pkt->size);
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
        save_packet(ar, left_color);
        save_packet(ar, right_color);
        ar << use_depth;
        if (use_depth) {
            save_packet(ar, left_depth);
            save_packet(ar, right_depth);
        }
        ar << pose;
        ar << sent_time;
    }

    template<class Archive>
    void load(Archive& ar, const unsigned int version) {
        ar >> boost::serialization::base_object<switchboard::event>(*this);
        left_color = av_packet_alloc();
        load_packet(ar, left_color);
        right_color = av_packet_alloc();
        load_packet(ar, right_color);
        ar >> use_depth;
        if (use_depth) {
            left_depth = av_packet_alloc();
            load_packet(ar, left_depth);
            right_depth = av_packet_alloc();
            load_packet(ar, right_depth);
        }
        ar >> pose;
        ar >> sent_time;
    }

    compressed_frame() = default;

    compressed_frame(AVPacket* left_color, AVPacket* right_color, const fast_pose_type& pose, uint64_t sent_time)
        : left_color(left_color)
        , right_color(right_color)
        , use_depth(false)
        , left_depth(nullptr)
        , right_depth(nullptr)
        , pose(pose)
        , sent_time(sent_time) { }

    compressed_frame(AVPacket* left_color, AVPacket* right_color, AVPacket* left_depth, AVPacket* right_depth, const fast_pose_type& pose, uint64_t sent_time)
        : left_color(left_color)
        , right_color(right_color)
        , use_depth(true)
        , left_depth(left_depth)
        , right_depth(right_depth)
        , pose(pose)
        , sent_time(sent_time) { }
};
} // namespace ILLIXR

template<class Archive>
void serialize(Archive& ar, ILLIXR::time_point& tp, const unsigned int version) {
    ar(tp._m_time_since_epoch);
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
CEREAL_FORCE_DYNAMIC_INIT(illixr_serializable_data)
#endif // ILLIXR_SERIALIZABLE_DATA_HPP
