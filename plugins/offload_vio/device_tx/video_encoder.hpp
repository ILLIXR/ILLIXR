#pragma once
#define VIO
#include "illixr/video_encoder.hpp"

#include <condition_variable>

namespace ILLIXR {

class vio_video_encoder : public video_encoder {
public:
    explicit vio_video_encoder(FrameCallback callback)
        : video_encoder(callback) { }

    void enqueue(cv::Mat& img0, cv::Mat& img1) override;

    GstFlowReturn cb_appsink(GstElement* sink) override;

private:
    unsigned int num_samples_ = 0;
    std::condition_variable pipeline_sync_;
};

#undef VIO
} // namespace ILLIXR
