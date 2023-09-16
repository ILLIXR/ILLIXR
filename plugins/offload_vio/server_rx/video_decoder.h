//
// Created by steven on 7/8/22.
//

#ifndef PLUGIN_VIDEO_DECODER_H
#define PLUGIN_VIDEO_DECODER_H

#include "gst/gst.h"

#include <condition_variable>
#include <opencv2/core/mat.hpp>
#include <queue>
#include <utility>

namespace ILLIXR {

class video_decoder {
private:
    unsigned int                              _num_samples = 0;
    std::function<void(cv::Mat&&, cv::Mat&&)> _callback;
    GstElement*                               _pipeline_img0;
    GstElement*                               _pipeline_img1;
    GstElement*                               _appsrc_img0;
    GstElement*                               _appsrc_img1;
    GstElement*                               _appsink_img0;
    GstElement*                               _appsink_img1;

    std::condition_variable _pipeline_sync;
    std::mutex              _pipeline_sync_mutex;
    GstMapInfo              _img0_map;
    GstMapInfo              _img1_map;
    bool                    _img0_ready = false;
    bool                    _img1_ready = false;

    void create_pipelines();

public:
    explicit video_decoder(std::function<void(cv::Mat&&, cv::Mat&&)> callback);

    void init();

    void enqueue(std::string& img0, std::string& img1);

    GstFlowReturn cb_appsink(GstElement* sink);
};

} // namespace ILLIXR

#endif // PLUGIN_VIDEO_DECODER_H
