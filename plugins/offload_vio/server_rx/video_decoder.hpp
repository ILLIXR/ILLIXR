#pragma once
#define VIO
#include "illixr/video_decoder.hpp"

#include <queue>
#include <utility>

namespace ILLIXR {

class vio_video_decoder : public video_decoder {
public:
    explicit vio_video_decoder(DecodeCallback callback)
        : video_decoder(callback) { }

    void enqueue(std::string& img0, std::string& img1) override;

private:
    unsigned int num_samples_ = 0;
};

} // namespace ILLIXR

#undef VIO
