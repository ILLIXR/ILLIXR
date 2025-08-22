#pragma once

#include "error_util.hpp"
#include "gst/gst.h"

#include <functional>
#include <mutex>
#include <opencv2/core/mat.hpp>

#if defined ADA
    #define ILLIXR_BITRATE  524288
    #define IMG_WIDTH       640
    #define IMG_HEIGHT      480
    #define ILLIXR_ENCODING "nvv4l2h265enc"
    #define TYPE_FRACTION   30

#elif defined VIO
    #define ILLIXR_BITRATE  5242880
    #define ILLIXR_ENCODING "nvv4l2h264enc"
    #define TYPE_FRACTION   0
// #define ZED

    #ifdef ZED
        #define IMG_WIDTH  672
        #define IMG_HEIGHT 376
    #else
        #define IMG_WIDTH  752
        #define IMG_HEIGHT 480
    #endif

#endif

// Alternative encoding bitrates
// 50Mbps = 52428800
// 20Mbps = 20971520
// 10Mbps = 10485760
// 5Mbps = 5242880
// 2Mbps = 2097152
// 0.5Mbps = 524288
// 0.1Mbps = 104857

namespace ILLIXR {

// pyh: callback invoked when both encoded outputs are ready (e.g., MSB/LSB planes)
using FrameCallback = std::function<void(const GstMapInfo&, const GstMapInfo&)>;

class video_encoder {
public:
    explicit video_encoder(FrameCallback callback)
        : callback_(std::move(callback)) { }

    void init() {
        create_pipelines();
    }

    virtual void enqueue(cv::Mat& img0, cv::Mat& img1) = 0;

    virtual GstFlowReturn cb_appsink(GstElement* sink) {
        (void) sink;
        return GST_FLOW_CUSTOM_ERROR;
    }

    virtual GstFlowReturn cb_appsink_msb(GstElement* sink) {
        (void) sink;
        return GST_FLOW_CUSTOM_ERROR;
    }

    virtual GstFlowReturn cb_appsink_lsb(GstElement* sink) {
        (void) sink;
        return GST_FLOW_CUSTOM_ERROR;
    }

    virtual ~video_encoder() = default;

protected:
    void create_pipelines();

    // gstreamer objects
    GstElement* pipeline_img0_{};
    GstElement* pipeline_img1_{};
    GstElement* appsrc_img0_{};
    GstElement* appsrc_img1_{};
    GstElement* appsink_img0_{};
    GstElement* appsink_img1_{};

    std::mutex    pipeline_sync_mutex_;
    GstMapInfo    img0_map_{};
    GstMapInfo    img1_map_{};
    bool          img0_ready_ = false;
    bool          img1_ready_ = false;
    FrameCallback callback_;

private:
#if defined ADA

    static GstFlowReturn cb_new_sample_0(GstElement* appsink, gpointer user_data) {
        return reinterpret_cast<video_encoder*>(user_data)->cb_appsink_msb(appsink);
    }

    static GstFlowReturn cb_new_sample_1(GstElement* appsink, gpointer user_data) {
        return reinterpret_cast<video_encoder*>(user_data)->cb_appsink_lsb(appsink);
    }
#elif defined VIO
    static GstFlowReturn cb_new_sample_0(GstElement* appsink, gpointer* user_data) {
        return reinterpret_cast<video_encoder*>(user_data)->cb_appsink(appsink);
    }

    static GstFlowReturn cb_new_sample_1(GstElement* appsink, gpointer* user_data) {
        return reinterpret_cast<video_encoder*>(user_data)->cb_appsink(appsink);
    }

#endif
};

void video_encoder::create_pipelines() {
    gst_init(nullptr, nullptr);

    // ADA: 0 MSB image encoding pipeline
    // ADA: 1 LSB image encoding pipeline
    appsrc_img0_  = gst_element_factory_make("appsrc", "appsrc_img0");
    appsrc_img1_  = gst_element_factory_make("appsrc", "appsrc_img1");
    appsink_img0_ = gst_element_factory_make("appsink", "appsink_img0");
    appsink_img1_ = gst_element_factory_make("appsink", "appsink_img1");

    auto nvvideoconvert_0 = gst_element_factory_make("nvvideoconvert", "nvvideoconvert0");
    auto nvvideoconvert_1 = gst_element_factory_make("nvvideoconvert", "nvvideoconvert1");

    auto encoder_img0 = gst_element_factory_make(ILLIXR_ENCODING, "encoder_img0");
    auto encoder_img1 = gst_element_factory_make(ILLIXR_ENCODING, "encoder_img1");

    auto caps_8uc1 =
            gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "GRAY8", "framerate", GST_TYPE_FRACTION, TYPE_FRACTION,
                                1, "width", G_TYPE_INT, IMG_WIDTH, "height", G_TYPE_INT, IMG_HEIGHT, NULL);

    g_object_set(G_OBJECT(appsrc_img0_), "caps", caps_8uc1, nullptr);
    g_object_set(G_OBJECT(appsrc_img1_), "caps", caps_8uc1, nullptr);
    gst_caps_unref(caps_8uc1);

    // set bitrate from environment variables
    // g_object_set(G_OBJECT(encoder_img0), "bitrate", std::stoi(sb->get_env("ILLIXR_BITRATE")), nullptr, 10);
    // g_object_set(G_OBJECT(encoder_img1), "bitrate", std::stoi(sb->get_env("ILLIXR_BITRATE")), nullptr, 10);

    // set bitrate from defined variables
    g_object_set(G_OBJECT(encoder_img0), "bitrate", ILLIXR_BITRATE, nullptr);
#if defined VIO
    g_object_set(G_OBJECT(encoder_img1), "bitrate", ILLIXR_BITRATE, nullptr);
        g_object_set(G_OBJECT(appsrc_img0_), "stream-type", 0, "format", GST_FORMAT_BYTES, "is-live", TRUE, nullptr);
        g_object_set(G_OBJECT(appsrc_img1_), "stream-type", 0, "format", GST_FORMAT_BYTES, "is-live", TRUE, nullptr);
#elif defined ADA

    // this is for 4(lossless setting) 2(default)
    g_object_set(G_OBJECT(encoder_img0), "tuning-info-id", 4, nullptr);
    g_object_set(G_OBJECT(encoder_img1), "tuning-info-id", 2, nullptr);

    // pyh: Set to a large value to avoid periodic latency spikes from keyframe insertion.
    // should be safe for our use case because we don't need random access to old frames.
    g_object_set(G_OBJECT(encoder_img0), "idrinterval", 25600, nullptr);
    g_object_set(G_OBJECT(encoder_img1), "idrinterval", 25600, nullptr);

    g_object_set(G_OBJECT(encoder_img0), "iframeinterval", 25600, nullptr);
    g_object_set(G_OBJECT(encoder_img1), "iframeinterval", 25600, nullptr);

    g_object_set(G_OBJECT(appsrc_img0_), "is-live", TRUE, "format", GST_FORMAT_TIME, "do-timestamp", FALSE, "block",
                 TRUE, // backpressure instead of queue explosion
                 nullptr);
    g_object_set(G_OBJECT(appsrc_img1_), "is-live", TRUE, "format", GST_FORMAT_TIME, "do-timestamp", FALSE, "block", TRUE,
                 nullptr);
#endif
    g_object_set(appsink_img0_, "emit-signals", TRUE, "sync", FALSE, nullptr);
    g_object_set(appsink_img1_, "emit-signals", TRUE, "sync", FALSE, nullptr);

    g_signal_connect(appsink_img0_, "new-sample", G_CALLBACK(cb_new_sample_0), this);
    g_signal_connect(appsink_img1_, "new-sample", G_CALLBACK(cb_new_sample_1), this);

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
} // namespace ILLIXR
