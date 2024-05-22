//
// Created by steven on 11/5/23.
//

#ifndef ILLIXR_SERIALIZABLE_DATA_HPP
#define ILLIXR_SERIALIZABLE_DATA_HPP

#include "data_format.hpp"
#include "switchboard.hpp"

#include <boost/serialization/binary_object.hpp>
#include <boost/serialization/export.hpp>

#ifdef AVUTIL_AVCONFIG_H
extern "C" {
    #include "libavcodec_illixr/avcodec.h"
    #include "libavformat_illixr/avformat.h"
    #include "libavutil_illixr/hwcontext.h"
    #include "libavutil_illixr/opt.h"
    #include "libavutil_illixr/pixdesc.h"
}
#endif

namespace ILLIXR {
struct compressed_frame : public switchboard::event {
    bool  nalu_only;
    char* left_color_nalu;
    char* right_color_nalu;
    char* left_depth_nalu;
    char* right_depth_nalu;
    int   left_color_nalu_size;
    int   right_color_nalu_size;
    int   left_depth_nalu_size;
    int   right_depth_nalu_size;

    bool use_depth;
#ifdef AVUTIL_AVCONFIG_H
    AVPacket* left_color;
    AVPacket* right_color;

    AVPacket* left_depth;
    AVPacket* right_depth;
#endif

    fast_pose_type pose;
    uint64_t       sent_time;

    friend class boost::serialization::access;

#ifdef AVUTIL_AVCONFIG_H
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
#endif

    template<class Archive>
    void save(Archive& ar, const unsigned int version) const {
        ar << boost::serialization::base_object<switchboard::event>(*this);
        ar << nalu_only;
#ifdef AVUTIL_AVCONFIG_H
        if (nalu_only) {
            ar << left_color->size;
            ar << right_color->size;
            ar << boost::serialization::make_array(left_color->data, left_color->size);
            ar << boost::serialization::make_array(right_color->data, right_color->size);
            if (use_depth) {
                ar << left_depth->size;
                ar << right_depth->size;
                ar << boost::serialization::make_array(left_depth->data, left_depth->size);
                ar << boost::serialization::make_array(right_depth->data, right_depth->size);
            }
        } else {
            save_packet(ar, left_color);
            save_packet(ar, right_color);
            ar << use_depth;
            if (use_depth) {
                save_packet(ar, left_depth);
                save_packet(ar, right_depth);
            }
        }
#else
        assert(false && "Not compiled with libav");
#endif

        ar << pose;
        ar << sent_time;
    }

    template<class Archive>
    void load(Archive& ar, const unsigned int version) {
        ar >> boost::serialization::base_object<switchboard::event>(*this);
        ar >> nalu_only;
        if (nalu_only) {
            ar >> left_color_nalu_size;
            ar >> right_color_nalu_size;
            left_color_nalu  = (char*) malloc(left_color_nalu_size);
            right_color_nalu = (char*) malloc(right_color_nalu_size);
            ar >> boost::serialization::make_array(left_color_nalu, left_color_nalu_size);
            ar >> boost::serialization::make_array(right_color_nalu, right_color_nalu_size);
            if (use_depth) {
                ar >> left_depth_nalu_size;
                ar >> right_depth_nalu_size;
                ar >> boost::serialization::make_array(left_depth_nalu, left_depth_nalu_size);
                ar >> boost::serialization::make_array(right_depth_nalu, right_depth_nalu_size);
            }
        } else {
#ifdef AVUTIL_AVCONFIG_H

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
#else
            assert(false && "Not compiled with libav");
#endif
        }

        ar >> pose;
        ar >> sent_time;
    }

    BOOST_SERIALIZATION_SPLIT_MEMBER()

    compressed_frame() = default;

#ifdef AVUTIL_AVCONFIG_H
    compressed_frame(AVPacket* left_color, AVPacket* right_color, const fast_pose_type& pose, uint64_t sent_time,
                     bool nalu_only = false)
        : left_color(left_color)
        , right_color(right_color)
        , use_depth(false)
        , left_depth(nullptr)
        , right_depth(nullptr)
        , pose(pose)
        , sent_time(sent_time)
        , nalu_only(nalu_only){ }

    compressed_frame(AVPacket* left_color, AVPacket* right_color, AVPacket* left_depth, AVPacket* right_depth,
                     const fast_pose_type& pose, uint64_t sent_time, bool nalu_only = false)
        : left_color(left_color)
        , right_color(right_color)
        , use_depth(true)
        , left_depth(left_depth)
        , right_depth(right_depth)
        , pose(pose)
        , sent_time(sent_time)
        , nalu_only(nalu_only){ }
#endif
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
