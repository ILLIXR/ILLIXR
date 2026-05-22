#pragma once

#include "illixr/data_format/frame.hpp"
#include "illixr/data_format/serialization/head_pose.hpp"
#include "misc.hpp"

#include <boost/serialization/binary_object.hpp>
#include <boost/serialization/split_free.hpp>
#include <boost/serialization/vector.hpp>
#include <spdlog/spdlog.h>
#ifdef ILLIXR_LIBAV
extern "C" {
#    include "libavcodec_illixr/avcodec.h"
}
#endif

// ---------------------------------------------------------------------------
// Packet helpers - identical logic to the member functions in frame.hpp but
// as free functions so they can be used from the non-member serializers below.
// ---------------------------------------------------------------------------
namespace ILLIXR::detail {

#ifdef ILLIXR_LIBAV
template<class Archive>
static void save_packet(Archive& ar, AVPacket* pkt) {
    int32_t pkt_size = pkt->size; // Use fixed-width type
    ar << pkt_size;
    ar << boost::serialization::make_array(pkt->data, pkt->size);
    ar << pkt->pts;
    ar << pkt->dts;
    ar << pkt->stream_index;
    ar << pkt->flags;
    ar << pkt->duration;
    ar << pkt->pos;
    ar << pkt->time_base.num;
    ar << pkt->time_base.den;
    int32_t side_data_elems = pkt->side_data_elems; // Use fixed-width type
    ar << side_data_elems;
    for (int i = 0; i < pkt->side_data_elems; i++) {
        int32_t  side_data_type = static_cast<int32_t>(pkt->side_data[i].type);
        uint64_t side_data_size = pkt->side_data[i].size;
        ar << side_data_type;
        ar << side_data_size;
        ar << boost::serialization::make_array(pkt->side_data[i].data, pkt->side_data[i].size);
    }
}

template<class Archive>
static void load_packet(Archive& ar, AVPacket* pkt) {
    int32_t pkt_size; // Use fixed-width type
    ar >> pkt_size;
    pkt->size = pkt_size;
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
    int32_t side_data_elems; // Use fixed-width type
    ar >> side_data_elems;
    pkt->side_data_elems = side_data_elems;
    pkt->side_data       = (AVPacketSideData*) malloc(sizeof(AVPacketSideData) * pkt->side_data_elems);
    for (int i = 0; i < pkt->side_data_elems; i++) {
        int32_t  side_data_type;
        uint64_t side_data_size;
        ar >> side_data_type;
        ar >> side_data_size;
        pkt->side_data[i].type = static_cast<AVPacketSideDataType>(side_data_type);
        pkt->side_data[i].size = static_cast<size_t>(side_data_size);
        pkt->side_data[i].data = (uint8_t*) malloc(pkt->side_data[i].size);
        ar >> boost::serialization::make_array(pkt->side_data[i].data, pkt->side_data[i].size);
    }
}

#elif defined(NVENC_ENCODER) || defined(NVDEC_DECODER)

template<class Archive>
static void save_packet(Archive& ar, const std::vector<uint8_t>& pkt) {
    uint64_t vec_size = pkt.size(); // Changed from size_t to uint64_t for cross-platform compatibility
    ar << vec_size;
    ar << boost::serialization::make_array(pkt.data(), pkt.size());
}

template<class Archive>
static void load_packet(Archive& ar, std::vector<uint8_t>& pkt) {
    uint64_t vec_size; // Changed from size_t to uint64_t for cross-platform compatibility
    ar >> vec_size;
    pkt.resize(static_cast<size_t>(vec_size));
    ar >> boost::serialization::make_array(pkt.data(), static_cast<size_t>(vec_size));
}
#endif

} // namespace ILLIXR::detail

// ---------------------------------------------------------------------------
// Non-member serializers in boost::serialization namespace
// ---------------------------------------------------------------------------
namespace boost::serialization {

template<class Archive>
void save(Archive& ar, const ILLIXR::data_format::compressed_frame& f, const unsigned int version) {
    (void) version;
    ar << boost::serialization::base_object<ILLIXR::switchboard::event>(f);
    ar << f.nalu_only;
    ar << f.use_depth;
    ar << f.use_motion_vectors;
#ifdef ILLIXR_LIBAV
    if (f.nalu_only) {
        int32_t left_size  = f.left_color->size;  // Use fixed-width type
        int32_t right_size = f.right_color->size; // Use fixed-width type
        ar << left_size;
        ar << right_size;
        ar << boost::serialization::make_array(f.left_color->data, f.left_color->size);
        ar << boost::serialization::make_array(f.right_color->data, f.right_color->size);
        if (f.use_depth) {
            int32_t left_depth_size  = f.left_depth->size;  // Use fixed-width type
            int32_t right_depth_size = f.right_depth->size; // Use fixed-width type
            ar << left_depth_size;
            ar << right_depth_size;
            ar << boost::serialization::make_array(f.left_depth->data, f.left_depth->size);
            ar << boost::serialization::make_array(f.right_depth->data, f.right_depth->size);
        }
        if (f.use_motion_vectors) {
            int32_t left_mv_size  = f.left_motion_vec->size;
            int32_t right_mv_size = f.right_motion_vec->size;
            ar << left_mv_size;
            ar << right_mv_size;
            ar << boost::serialization::make_array(f.left_motion_vec->data, f.left_motion_vec->size);
            ar << boost::serialization::make_array(f.right_motion_vec->data, f.right_motion_vec->size);
        }
    } else {
#endif
#if defined(ILLIXR_LIBAV) || defined(NVENC_ENCODER) || defined(NVDEC_DECODER)
        ILLIXR::detail::save_packet(ar, f.left_color);
        ILLIXR::detail::save_packet(ar, f.right_color);
        if (f.use_depth) {
            ILLIXR::detail::save_packet(ar, f.left_depth);
            ILLIXR::detail::save_packet(ar, f.right_depth);
        }
        if (f.use_motion_vectors) {
            ILLIXR::detail::save_packet(ar, f.left_motion_vec);
            ILLIXR::detail::save_packet(ar, f.right_motion_vec);
        }
#    ifdef ILLIXR_LIBAV
    }
#    endif
#else
    static_assert(false, "Not compiled with libav or NVENC/NVDEC");
#endif

#ifdef USING_OPENXR
    ar << f.pose[0];
    ar << f.pose[1];
#else
    ar << f.pose;
#endif
    ar << f.near_z;
    ar << f.far_z;
#ifdef USING_OPENXR
    ar << f.fov_left[0];
    ar << f.fov_left[1];
    ar << f.fov_right[0];
    ar << f.fov_right[1];
    ar << f.fov_up[0];
    ar << f.fov_up[1];
    ar << f.fov_down[0];
    ar << f.fov_down[1];
#endif
    ar << f.sent_time;
    ar << f.frame_number;
    ar << f.pose_id;
    ar << f.encode_time;
    ar << f.is_keyframe;
    ar << f.magic;
}

template<class Archive>
void load(Archive& ar, ILLIXR::data_format::compressed_frame& f, const unsigned int version) {
    (void) version;
    ar >> boost::serialization::base_object<ILLIXR::switchboard::event>(f);
    ar >> f.nalu_only;
    ar >> f.use_depth;
    ar >> f.use_motion_vectors;

    if (f.nalu_only) {
        ar >> f.left_color_nalu_size;
        ar >> f.right_color_nalu_size;
        f.left_color_nalu  = (char*) malloc(f.left_color_nalu_size);
        f.right_color_nalu = (char*) malloc(f.right_color_nalu_size);
        ar >> boost::serialization::make_array(f.left_color_nalu, f.left_color_nalu_size);
        ar >> boost::serialization::make_array(f.right_color_nalu, f.right_color_nalu_size);
        if (f.use_depth) {
            ar >> f.left_depth_nalu_size;
            ar >> f.right_depth_nalu_size;
            f.left_depth_nalu  = (char*) malloc(f.left_depth_nalu_size);
            f.right_depth_nalu = (char*) malloc(f.right_depth_nalu_size);
            ar >> boost::serialization::make_array(f.left_depth_nalu, f.left_depth_nalu_size);
            ar >> boost::serialization::make_array(f.right_depth_nalu, f.right_depth_nalu_size);
        }
    } else {
#if defined(ILLIXR_LIBAV) || defined(NVENC_ENCODER) || defined(NVDEC_DECODER)

#    ifdef ILLIXR_LIBAV
        f.left_color  = av_packet_alloc();
        f.right_color = av_packet_alloc();
#    endif
        ILLIXR::detail::load_packet(ar, f.left_color);
        ILLIXR::detail::load_packet(ar, f.right_color);

        if (f.use_depth) {
#    ifdef ILLIXR_LIBAV
            f.left_depth  = av_packet_alloc();
            f.right_depth = av_packet_alloc();
#    endif
            ILLIXR::detail::load_packet(ar, f.left_depth);
            ILLIXR::detail::load_packet(ar, f.right_depth);
        }
        if (f.use_motion_vectors) {
#    ifdef ILLIXR_LIBAV
            f.left_motion_vec  = av_packet_alloc();
            f.right_motion_vec = av_packet_alloc();
#    endif
            ILLIXR::detail::load_packet(ar, f.left_motion_vec);
            ILLIXR::detail::load_packet(ar, f.right_motion_vec);
        }
#else
        static_assert(false, "Not compiled with libav or NVENC/NVDEC");
#endif
    }
#ifdef USING_OPENXR
    ar >> f.pose[0];
    ar >> f.pose[1];
#else
    ar >> f.pose;
#endif

    ar >> f.near_z;
    ar >> f.far_z;
#ifdef USING_OPENXR
    ar >> f.fov_left[0];
    ar >> f.fov_left[1];
    ar >> f.fov_right[0];
    ar >> f.fov_right[1];
    ar >> f.fov_up[0];
    ar >> f.fov_up[1];
    ar >> f.fov_down[0];
    ar >> f.fov_down[1];
#endif

    ar >> f.sent_time;
    ar >> f.frame_number;
    ar >> f.pose_id;
    ar >> f.encode_time;
    ar >> f.is_keyframe;
    ar >> f.magic;
    if (f.magic != 0xdeadbeef) {
        throw std::runtime_error("compressed_frame: magic number mismatch");
    }
}

// Register the split free functions with Boost
template<class Archive>
void serialize(Archive& ar, ILLIXR::data_format::compressed_frame& f, const unsigned int version) {
    boost::serialization::split_free(ar, f, version);
}

} // namespace boost::serialization

BOOST_CLASS_EXPORT_KEY(ILLIXR::data_format::compressed_frame)
