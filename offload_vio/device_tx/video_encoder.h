//
// Created by steven on 7/3/22.
//

#ifndef ILLIXR_COMPRESSION_VIDEO_ENCODER_H
#define ILLIXR_COMPRESSION_VIDEO_ENCODER_H

#include "gst/gst.h"
#include <opencv2/core/mat.hpp>
#include <utility>
#include <queue>
#include <condition_variable>

namespace ILLIXR {

    class video_encoder {
    private:
        std::function<void(const GstMapInfo &, const GstMapInfo &)> _callback;

//        unsigned int _sample_rate = 15;
        unsigned int _num_samples = 0;
//        GstClockTime _last_timestamp = GST_CLOCK_TIME_NONE;

        GstElement *_pipeline_img0;
        GstElement *_pipeline_img1;
        GstElement *_appsrc_img0;
        GstElement *_appsrc_img1;
        GstElement *_appsink_img0;
        GstElement *_appsink_img1;

        std::condition_variable _pipeline_sync;
        std::mutex _pipeline_sync_mutex;
        GstMapInfo _img0_map;
        GstMapInfo _img1_map;
        bool _img0_ready = false;
        bool _img1_ready = false;

        void create_pipelines();
    public:
        explicit video_encoder(std::function<void(const GstMapInfo&, const GstMapInfo&)> callback);

        void init();

        void enqueue(cv::Mat& img0, cv::Mat& img1);

        GstFlowReturn cb_appsink(GstElement *sink);
    };

} // ILLIXR

#endif //ILLIXR_COMPRESSION_VIDEO_ENCODER_H
