#pragma once

#include "common.hpp"

#include <limits>

namespace ILLIXR::encoding {

class rgb_decoder : public encoding_common {
public:
    rgb_decoder();

    void rgb2depth(const cv::Mat& rgb, cv::Mat& depth, float zmin, float zmax);

private:
    void create_dec_lut();

    // Decode with LUT
    void decode_lut(const cv::Mat& rgb, cv::Mat& depth);

    void decode(const cv::Mat& rgb, cv::Mat& depth);

    cv::Mat     dec_lut_;
    float       err_depth_      = std::numeric_limits<float>::quiet_NaN();
    const float min_value_      = 0.4;
    const float min_saturation_ = 0.4;
};
} // namespace ILLIXR::encoding
