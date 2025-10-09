#pragma once
#define ADA
#include "illixr/video_encoder.hpp"

namespace ILLIXR {

class ada_video_encoder : public video_encoder {
public:
    explicit ada_video_encoder(FrameCallback callback)
        : video_encoder(callback) { }

    void          enqueue(cv::Mat& img0, cv::Mat& img1) override;
    GstFlowReturn cb_appsink_msb(GstElement* sink) override;
    GstFlowReturn cb_appsink_lsb(GstElement* sink) override;

private:
    GstSample* samp0_{};
    GstSample* samp1_{};
    guint64    frame_idx_ = 0;
};

#undef ADA
} // namespace ILLIXR
