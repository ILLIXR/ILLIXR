//
// Created by steven on 7/8/22.
//

#include "video_decoder.hpp"

#include "illixr/error_util.hpp"

#include <gst/app/gstappsrc.h>
#include <utility>

namespace ILLIXR {
#define ZED

#ifdef ZED
    #define IMG_WIDTH  672
    #define IMG_HEIGHT 376
#else
    #define IMG_WIDTH  752
    #define IMG_HEIGHT 480
#endif

GstFlowReturn cb_new_sample(GstElement* appsink, gpointer* user_data) {
    return reinterpret_cast<video_decoder*>(user_data)->cb_appsink(appsink);
}

void video_decoder::create_pipelines() {
    gst_init(nullptr, nullptr);

    appsrc_img0_  = gst_element_factory_make("appsrc", "appsrc_img0");
    appsrc_img1_  = gst_element_factory_make("appsrc", "appsrc_img1");
    appsink_img0_ = gst_element_factory_make("appsink", "appsink_img0");
    appsink_img1_ = gst_element_factory_make("appsink", "appsink_img1");

    auto nvvideoconvert_0 = gst_element_factory_make("nvvideoconvert", "nvvideoconvert0");
    auto nvvideoconvert_1 = gst_element_factory_make("nvvideoconvert", "nvvideoconvert1");

    auto decoder_img0 = gst_element_factory_make("nvv4l2decoder", "decoder_img0");
    auto decoder_img1 = gst_element_factory_make("nvv4l2decoder", "decoder_img1");

    auto caps_filter_0 = gst_element_factory_make("capsfilter", "caps_filter_0");
    auto caps_filter_1 = gst_element_factory_make("capsfilter", "caps_filter_1");

    g_object_set(G_OBJECT(caps_filter_0), "caps", gst_caps_from_string("video/x-raw,format=GRAY8"), nullptr);
    g_object_set(G_OBJECT(caps_filter_1), "caps", gst_caps_from_string("video/x-raw,format=GRAY8"), nullptr);

    // create caps with width and height
    auto caps_x265 = gst_caps_new_simple("video/x-h264", "stream-format", G_TYPE_STRING, "byte-stream", "alignment",
                                         G_TYPE_STRING, "au", nullptr);
    g_object_set(G_OBJECT(appsrc_img0_), "caps", caps_x265, nullptr);
    g_object_set(G_OBJECT(appsrc_img1_), "caps", caps_x265, nullptr);
    gst_caps_unref(caps_x265);

    g_object_set(G_OBJECT(appsrc_img0_), "stream-type", 0, "format", GST_FORMAT_BYTES, "is-live", TRUE, nullptr);
    g_object_set(G_OBJECT(appsrc_img1_), "stream-type", 0, "format", GST_FORMAT_BYTES, "is-live", TRUE, nullptr);

    g_object_set(G_OBJECT(decoder_img0), "low-latency-mode", TRUE, nullptr);
    g_object_set(G_OBJECT(decoder_img1), "low-latency-mode", TRUE, nullptr);

    g_object_set(appsink_img0_, "emit-signals", TRUE, "sync", FALSE, nullptr);
    g_object_set(appsink_img1_, "emit-signals", TRUE, "sync", FALSE, nullptr);

    g_signal_connect(appsink_img0_, "new-sample", G_CALLBACK(cb_new_sample), this);
    g_signal_connect(appsink_img1_, "new-sample", G_CALLBACK(cb_new_sample), this);

    pipeline_img0_ = gst_pipeline_new("pipeline_img0");
    pipeline_img1_ = gst_pipeline_new("pipeline_img1");

    gst_bin_add_many(GST_BIN(pipeline_img0_), appsrc_img0_, decoder_img0, nvvideoconvert_0, caps_filter_0, appsink_img0_,
                     nullptr);
    gst_bin_add_many(GST_BIN(pipeline_img1_), appsrc_img1_, decoder_img1, nvvideoconvert_1, caps_filter_1, appsink_img1_,
                     nullptr);

    // link elements
    if (!gst_element_link_many(appsrc_img0_, decoder_img0, nvvideoconvert_0, caps_filter_0, appsink_img0_, nullptr) ||
        !gst_element_link_many(appsrc_img1_, decoder_img1, nvvideoconvert_1, caps_filter_1, appsink_img1_, nullptr)) {
        abort("Failed to link elements");
    }

    gst_element_set_state(pipeline_img0_, GST_STATE_PLAYING);
    gst_element_set_state(pipeline_img1_, GST_STATE_PLAYING);
}

video_decoder::video_decoder(std::function<void(cv::Mat&&, cv::Mat&&)> callback)
    : callback_(std::move(callback)) { }

void video_decoder::init() {
    create_pipelines();
}

void video_decoder::enqueue(std::string& img0, std::string& img1) {
    auto buffer_img0 =
        gst_buffer_new_wrapped_full(GST_MEMORY_FLAG_READONLY, img0.data(), img0.size(), 0, img0.size(), nullptr, nullptr);
    auto buffer_img1 =
        gst_buffer_new_wrapped_full(GST_MEMORY_FLAG_READONLY, img1.data(), img1.size(), 0, img1.size(), nullptr, nullptr);

    GST_BUFFER_OFFSET(buffer_img0) = num_samples_;
    GST_BUFFER_OFFSET(buffer_img1) = num_samples_;

    num_samples_++;

    auto ret_img0 = gst_app_src_push_buffer(reinterpret_cast<GstAppSrc*>(appsrc_img0_), buffer_img0);
    auto ret_img1 = gst_app_src_push_buffer(reinterpret_cast<GstAppSrc*>(appsrc_img1_), buffer_img1);

    if (ret_img0 != GST_FLOW_OK || ret_img1 != GST_FLOW_OK) {
        abort("Failed to push buffer");
    }
}

GstFlowReturn video_decoder::cb_appsink(GstElement* sink) {
    GstSample* sample;
    g_signal_emit_by_name(sink, "pull-sample", &sample);
    if (sample) {
        GstBuffer* buffer = gst_sample_get_buffer(sample);

        std::unique_lock<std::mutex> lock(pipeline_sync_mutex_); // lock acquired
        if (sink == appsink_img0_) {
            gst_buffer_map(buffer, &img0_map_, GST_MAP_READ);
            img0_ready_ = true;
        } else {
            gst_buffer_map(buffer, &img1_map_, GST_MAP_READ);
            img1_ready_ = true;
        }

        if (img0_ready_ && img1_ready_) {
            callback_(cv::Mat(IMG_HEIGHT, IMG_WIDTH, CV_8UC1, img0_map_.data),
                      cv::Mat(IMG_HEIGHT, IMG_WIDTH, CV_8UC1, img1_map_.data));
            img0_ready_ = false;
            img1_ready_ = false;
            lock.unlock(); // unlock and notify the waiting thread to clean up
            pipeline_sync_.notify_one();
        } else {
            pipeline_sync_.wait(lock, [&]() {
                return !img0_ready_ && !img1_ready_;
            });            // wait unlocks the mutex if condition is not met
            lock.unlock(); // wait has acquired the lock. unlock it and start cleaning up
        }

        if (sink == appsink_img0_) {
            gst_buffer_unmap(buffer, &img0_map_);
        } else {
            gst_buffer_unmap(buffer, &img1_map_);
        }
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }
    return GST_FLOW_ERROR;
}

} // namespace ILLIXR
