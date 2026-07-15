#pragma once

#include "gst/gst.h"
#include "illixr/error_util.hpp"

#include <condition_variable>
#include <functional>
#include <opencv2/core/mat.hpp>

#if defined ADA
    #define IMG_WIDTH  1
    #define IMG_HEIGHT 1
#elif defined VIO
// #define ZED

    #ifdef ZED
        #define IMG_WIDTH  672
        #define IMG_HEIGHT 376
    #else
        #define IMG_WIDTH  752
        #define IMG_HEIGHT 480
    #endif
#endif

namespace ILLIXR {

using DecodeCallback = std::function<void(cv::Mat&&, cv::Mat&&)>;

class video_decoder {
public:
    explicit video_decoder(DecodeCallback callback)
        : callback_(std::move(callback)) { }

    inline void init() {
        create_pipelines();
#ifdef ADA
        msb_owned_.create(480, 640, CV_8UC1);
        lsb_owned_.create(480, 640, CV_8UC1);
#endif
    }

    virtual void  enqueue(std::string& img0, std::string& img1) = 0;
    GstFlowReturn cb_appsink(GstElement* sink);

protected:
    void create_pipelines();

    DecodeCallback callback_;
    // gstreamer objects
    GstElement* pipeline_img0_{};
    GstElement* pipeline_img1_{};
    GstElement* appsrc_img0_{};
    GstElement* appsrc_img1_{};
    GstElement* appsink_img0_{};
    GstElement* appsink_img1_{};

    std::condition_variable pipeline_sync_;
    std::mutex              pipeline_sync_mutex_;
    GstMapInfo              img0_map_{};
    GstMapInfo              img1_map_{};
    bool                    img0_ready_ = false;
    bool                    img1_ready_ = false;

private:
    static GstFlowReturn cb_new_sample(GstElement* appsink, gpointer* user_data) {
        return reinterpret_cast<video_decoder*>(user_data)->cb_appsink(appsink);
    }

#ifdef ADA
    cv::Mat msb_owned_;
    cv::Mat lsb_owned_;
#endif
};

} // namespace ILLIXR
