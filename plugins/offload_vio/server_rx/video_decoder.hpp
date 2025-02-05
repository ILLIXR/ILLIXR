//
// Created by steven on 7/8/22.
//

#ifndef PLUGIN_VIDEO_DECODER_H
#define PLUGIN_VIDEO_DECODER_H

#include "gst/gst.h"

#include <condition_variable>
#include <functional>
#include <opencv2/core/mat.hpp>
#include <queue>
#include <utility>

namespace ILLIXR {

class video_decoder {
public:
    explicit video_decoder(std::function<void(cv::Mat&&, cv::Mat&&)> callback);

    void init();

    void enqueue(std::string& img0, std::string& img1);

    GstFlowReturn cb_appsink(GstElement* sink);

private:
    void create_pipelines();

    unsigned int                              num_samples_ = 0;
    std::function<void(cv::Mat&&, cv::Mat&&)> callback_;
    GstElement*                               pipeline_img0_{};
    GstElement*                               pipeline_img1_{};
    GstElement*                               appsrc_img0_{};
    GstElement*                               appsrc_img1_{};
    GstElement*                               appsink_img0_{};
    GstElement*                               appsink_img1_{};

    std::condition_variable pipeline_sync_;
    std::mutex              pipeline_sync_mutex_;
    GstMapInfo              img0_map_{};
    GstMapInfo              img1_map_{};
    bool                    img0_ready_ = false;
    bool                    img1_ready_ = false;
};

} // namespace ILLIXR

#endif // PLUGIN_VIDEO_DECODER_H
