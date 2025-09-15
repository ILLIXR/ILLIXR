#pragma once

#include "illixr/error_util.hpp"
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

    inline void init() {
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

    virtual ~video_encoder() {}

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

} // namespace ILLIXR
