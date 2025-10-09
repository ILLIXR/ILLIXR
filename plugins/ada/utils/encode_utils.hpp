#pragma once

#include "common.hpp"

namespace ILLIXR::encoding {

class rgb_encoder : public encoding_common {
public:
    rgb_encoder();

    // Encode depth to RGB
    void depth2rgb(const cv::Mat& depth, cv::Mat& rgb, float& zmin, float& zmax);

private:
    void create_enc_lut();

    void encode_lut(const cv::Mat& depth, cv::Mat& rgb, bool sanitized = false);

    void encode(const cv::Mat& depth, cv::Mat& rgb, bool sanitized = false);

    cv::Mat enc_lut_;
};
} // namespace ILLIXR::encoding
