#pragma once

#ifdef Success
    #undef Success // For 'Success' conflict
#endif

#include "illixr/data_format/pose.hpp"
#include "illixr/switchboard.hpp"

#include <array>
#include <boost/serialization/export.hpp>
#include <GL/gl.h>

#include <vulkan/vulkan.h>

#ifdef ILLIXR_LIBAV
extern "C" {
    #include "libavcodec_illixr/avcodec.h"
    #include "libavformat_illixr/avformat.h"
    #include "libavutil_illixr/hwcontext.h"
    #include "libavutil_illixr/opt.h"
    #include "libavutil_illixr/pixdesc.h"
}
#endif

namespace ILLIXR::data_format {
struct frame_to_be_saved : public switchboard::event {
    VkImage image;
    uint32_t width;
    uint32_t height;
    uint32_t frame_number;
    bool left;
    VkFence fence;
    std::string output_directory;
    int index;

    frame_to_be_saved() = default;

    frame_to_be_saved(VkImage image_, uint32_t width_, uint32_t height_, uint32_t frame_number_, bool left_,
                      VkFence fence_, std::string output_directory_, int index_)
        : image(image_)
        , width(width_)
        , height(height_)
        , frame_number(frame_number_)
        , left(left_)
        , fence(fence_)
        , output_directory(std::move(output_directory_)) 
        , index(index_) { }
};

// Using arrays as a swapchain
// Array of left eyes, array of right eyes
// This more closely matches the format used by Monado
struct [[maybe_unused]] rendered_frame : public switchboard::event {
    std::array<GLuint, 2> swapchain_indices{}; // Does not change between swaps in swapchain
    std::array<GLuint, 2> swap_indices{};      // Which element of the swapchain
    fast_pose_type        render_pose;         // The pose used when rendering this frame.
    time_point            sample_time{};
    time_point            render_time{};

    rendered_frame() = default;

    rendered_frame(std::array<GLuint, 2>&& swapchain_indices_, std::array<GLuint, 2>&& swap_indices_,
                   fast_pose_type render_pose_, time_point sample_time_, time_point render_time_)
        : swapchain_indices{swapchain_indices_}
        , swap_indices{swap_indices_}
        , render_pose(std::move(render_pose_))
        , sample_time(sample_time_)
        , render_time(render_time_) { }
};

struct compressed_frame : public switchboard::event {
    bool  nalu_only;
    char* left_color_nalu  = nullptr;
    char* right_color_nalu = nullptr;
    char* left_depth_nalu  = nullptr;
    char* right_depth_nalu = nullptr;
    int   left_color_nalu_size;
    int   right_color_nalu_size;
    int   left_depth_nalu_size;
    int   right_depth_nalu_size;

    bool use_depth;
#ifdef ILLIXR_LIBAV
    AVPacket* left_color;
    AVPacket* right_color;

    AVPacket* left_depth;
    AVPacket* right_depth;
#endif

    fast_pose_type pose;
    uint64_t       sent_time;
    long           magic = 0;

    friend class boost::serialization::access;

#ifdef ILLIXR_LIBAV
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
        (void) version;
        ar << boost::serialization::base_object<switchboard::event>(*this);
        ar << nalu_only;
        ar << use_depth;
#ifdef ILLIXR_LIBAV
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
        ar << magic;
    }

    template<class Archive>
    void load(Archive& ar, const unsigned int version) {
        (void) version;
        ar >> boost::serialization::base_object<switchboard::event>(*this);
        ar >> nalu_only;
        ar >> use_depth;
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
                left_depth_nalu  = (char*) malloc(left_depth_nalu_size);
                right_depth_nalu = (char*) malloc(right_depth_nalu_size);
                ar >> boost::serialization::make_array(left_depth_nalu, left_depth_nalu_size);
                ar >> boost::serialization::make_array(right_depth_nalu, right_depth_nalu_size);
            }
        } else {
#ifdef ILLIXR_LIBAV

            left_color = av_packet_alloc();
            load_packet(ar, left_color);
            right_color = av_packet_alloc();
            load_packet(ar, right_color);
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
        ar >> magic;
        if (magic != 0xdeadbeef) {
            throw std::runtime_error("Magic number mismatch");
        }
    }

    BOOST_SERIALIZATION_SPLIT_MEMBER()

    compressed_frame() = default;

#ifdef ILLIXR_LIBAV
    compressed_frame(AVPacket* left_color, AVPacket* right_color, const fast_pose_type& pose, uint64_t sent_time,
                     bool nalu_only = false)
        : nalu_only(nalu_only)
        , use_depth(false)
        , left_color(left_color)
        , right_color(right_color)
        , left_depth(nullptr)
        , right_depth(nullptr)
        , pose(pose)
        , sent_time(sent_time)
        , magic(0xdeadbeef) { }

    compressed_frame(AVPacket* left_color, AVPacket* right_color, AVPacket* left_depth, AVPacket* right_depth,
                     const fast_pose_type& pose, uint64_t sent_time, bool nalu_only = false)
        : nalu_only(nalu_only)
        , use_depth(true)
        , left_color(left_color)
        , right_color(right_color)
        , left_depth(left_depth)
        , right_depth(right_depth)
        , pose(pose)
        , sent_time(sent_time)
        , magic(0xdeadbeef) { }
#endif
    ~compressed_frame() {
        if (nalu_only && left_color_nalu != nullptr && right_color_nalu != nullptr) {
            free(left_color_nalu);
            free(right_color_nalu);
            if (use_depth) {
                free(left_depth_nalu);
                free(right_depth_nalu);
            }
        }
    }
};

} // namespace ILLIXR::data_format
