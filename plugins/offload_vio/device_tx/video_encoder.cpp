#include "video_encoder.hpp"

#include "illixr/error_util.hpp"

#include <chrono>
#include <gst/app/gstappsrc.h>
#include <thread>

namespace ILLIXR {
#define ZED

#ifdef ZED
    #define IMG_WIDTH  672
    #define IMG_HEIGHT 376
#else
    #define IMG_WIDTH  752
    #define IMG_HEIGHT 480
#endif

#define ILLIXR_BITRATE 5242880

// Alternative encoding bitrates
// 50Mbps = 52428800
// 20Mbps = 20971520
// 10Mbps = 10485760
// 5Mbps = 5242880
// 2Mbps = 2097152
// 0.5Mbps = 524288
// 0.1Mbps = 104857

GstFlowReturn cb_new_sample(GstElement* appsink, gpointer* user_data) {
    return reinterpret_cast<video_encoder*>(user_data)->cb_appsink(appsink);
}

video_encoder::video_encoder(std::function<void(const GstMapInfo&, const GstMapInfo&)> callback)
    : callback_(std::move(callback)) { }

void video_encoder::create_pipelines() {
    gst_init(nullptr, nullptr);

    appsrc_img0_  = gst_element_factory_make("appsrc", "appsrc_img0");
    appsrc_img1_  = gst_element_factory_make("appsrc", "appsrc_img1");
    appsink_img0_ = gst_element_factory_make("appsink", "appsink_img0");
    appsink_img1_ = gst_element_factory_make("appsink", "appsink_img1");

    auto nvvideoconvert_0 = gst_element_factory_make("nvvideoconvert", "nvvideoconvert0");
    auto nvvideoconvert_1 = gst_element_factory_make("nvvideoconvert", "nvvideoconvert1");

    auto encoder_img0 = gst_element_factory_make("nvv4l2h264enc", "encoder_img0");
    auto encoder_img1 = gst_element_factory_make("nvv4l2h264enc", "encoder_img1");

    auto caps_8uc1 = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "GRAY8", "framerate", GST_TYPE_FRACTION, 0, 1,
                                         "width", G_TYPE_INT, IMG_WIDTH, "height", G_TYPE_INT, IMG_HEIGHT, NULL);
    g_object_set(G_OBJECT(appsrc_img0_), "caps", caps_8uc1, nullptr);
    g_object_set(G_OBJECT(appsrc_img1_), "caps", caps_8uc1, nullptr);
    gst_caps_unref(caps_8uc1);

    // set bitrate from environment variables
    // g_object_set(G_OBJECT(encoder_img0), "bitrate", std::stoi(sb->get_env("ILLIXR_BITRATE")), nullptr, 10);
    // g_object_set(G_OBJECT(encoder_img1), "bitrate", std::stoi(sb->get_env("ILLIXR_BITRATE")), nullptr, 10);

    // set bitrate from defined variables
    g_object_set(G_OBJECT(encoder_img0), "bitrate", ILLIXR_BITRATE, nullptr);
    g_object_set(G_OBJECT(encoder_img1), "bitrate", ILLIXR_BITRATE, nullptr);

    g_object_set(G_OBJECT(appsrc_img0_), "stream-type", 0, "format", GST_FORMAT_BYTES, "is-live", TRUE, nullptr);
    g_object_set(G_OBJECT(appsrc_img1_), "stream-type", 0, "format", GST_FORMAT_BYTES, "is-live", TRUE, nullptr);

    g_object_set(appsink_img0_, "emit-signals", TRUE, "sync", FALSE, nullptr);
    g_object_set(appsink_img1_, "emit-signals", TRUE, "sync", FALSE, nullptr);

    g_signal_connect(appsink_img0_, "new-sample", G_CALLBACK(cb_new_sample), this);
    g_signal_connect(appsink_img1_, "new-sample", G_CALLBACK(cb_new_sample), this);

    pipeline_img0_ = gst_pipeline_new("pipeline_img0");
    pipeline_img1_ = gst_pipeline_new("pipeline_img1");

    gst_bin_add_many(GST_BIN(pipeline_img0_), appsrc_img0_, nvvideoconvert_0, encoder_img0, appsink_img0_, nullptr);
    gst_bin_add_many(GST_BIN(pipeline_img1_), appsrc_img1_, nvvideoconvert_1, encoder_img1, appsink_img1_, nullptr);

    // link elements
    if (!gst_element_link_many(appsrc_img0_, nvvideoconvert_0, encoder_img0, appsink_img0_, nullptr) ||
        !gst_element_link_many(appsrc_img1_, nvvideoconvert_1, encoder_img1, appsink_img1_, nullptr)) {
        abort("Failed to link elements");
    }

    gst_element_set_state(pipeline_img0_, GST_STATE_PLAYING);
    gst_element_set_state(pipeline_img1_, GST_STATE_PLAYING);
}

void video_encoder::enqueue(cv::Mat& img0, cv::Mat& img1) {
    // push cv mat into appsrc
    // print img0 size

    auto data_size      = img0.cols * img0.rows * img0.channels();
    int  size           = floor(data_size + ceil(img0.cols / 2.0) * ceil(img0.rows / 2.0) * 2);
    auto fill_zero_size = size - data_size;

    auto buffer_img0 = gst_buffer_new_and_alloc(size);
    auto buffer_img1 = gst_buffer_new_and_alloc(size);

    gst_buffer_fill(buffer_img0, 0, img0.data, data_size);
    gst_buffer_fill(buffer_img1, 0, img1.data, data_size);

    gst_buffer_memset(buffer_img0, data_size, 128, fill_zero_size);
    gst_buffer_memset(buffer_img1, data_size, 0, fill_zero_size);

    GST_BUFFER_OFFSET(buffer_img0) = num_samples_;
    GST_BUFFER_OFFSET(buffer_img1) = num_samples_;

    num_samples_++;

    auto ret_img0 = gst_app_src_push_buffer(reinterpret_cast<GstAppSrc*>(appsrc_img0_), buffer_img0);
    auto ret_img1 = gst_app_src_push_buffer(reinterpret_cast<GstAppSrc*>(appsrc_img1_), buffer_img1);
    if (ret_img0 != GST_FLOW_OK || ret_img1 != GST_FLOW_OK) {
        abort("Failed to push buffer");
    }
}

void video_encoder::init() {
    create_pipelines();
}

GstFlowReturn video_encoder::cb_appsink(GstElement* sink) {
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
            callback_(img0_map_, img1_map_);
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
