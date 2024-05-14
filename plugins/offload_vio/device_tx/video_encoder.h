//
// Created by steven on 7/3/22.
//

#ifndef ILLIXR_COMPRESSION_VIDEO_ENCODER_H
#define ILLIXR_COMPRESSION_VIDEO_ENCODER_H

#include "gst/gst.h"

#include <condition_variable>
#include <opencv2/core/mat.hpp>
#include <queue>
#include <utility>

namespace ILLIXR {

class video_encoder {
public:
    explicit video_encoder(std::function<void(const GstMapInfo&, const GstMapInfo&)> callback);

    void init();

    void enqueue(cv::Mat& img0, cv::Mat& img1);

    GstFlowReturn cb_appsink(GstElement* sink);

private:
    void create_pipelines();

    // unsigned int _sample_rate = 15;
    unsigned int num_samples_ = 0;
    // GstClockTime _last_timestamp = GST_CLOCK_TIME_NONE;

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

    std::function<void(const GstMapInfo&, const GstMapInfo&)> callback_;
};

} // namespace ILLIXR

#endif // ILLIXR_COMPRESSION_VIDEO_ENCODER_H
