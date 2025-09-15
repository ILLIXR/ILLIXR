#pragma once
#define ADA
#include "illixr/video_decoder.hpp"

#include <mutex>
#include <string>

namespace ILLIXR {

// pyh: App supplies a callback that receives the decoded MSB/LSB images.

class ada_video_decoder : public video_decoder {
public:
    explicit ada_video_decoder(DecodeCallback callback)
        : video_decoder(callback) { }

    void enqueue(std::string& img0, std::string& img1) override;

//private:
    // pyh reused buffers
//    cv::Mat msb_owned_;
//    cv::Mat lsb_owned_;
};

} // namespace ILLIXR

#undef ADA
