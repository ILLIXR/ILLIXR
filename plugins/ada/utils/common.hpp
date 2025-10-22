#pragma once

#include <opencv2/opencv.hpp>

constexpr int   QUANT_MAX  = 255;
constexpr float MAX_HUE    = 300.f;
constexpr int   NUM_UNIQUE = static_cast<int>(MAX_HUE / 60.f) * QUANT_MAX + 1;

namespace ILLIXR::encoding {

class encoding_common {
public:
    encoding_common();

protected:
    // quantize float[0., 1.] to uint8 [0, 255]
    void quantize(const cv::Mat& input, cv::Mat& output);

    // Dequantize uint8 [0, 255] to float [0. ,1.]
    void dequantize(const cv::Mat& input, cv::Mat& output);

    // RGB to HSV conversion
    void rgb2hsv(const cv::Mat& rgb, cv::Mat& hsv);

    // HSV to RGB conversion
    void hsv2rgb(const cv::Mat& hsv, cv::Mat& rgb);

    cv::Vec3f err_rgb_ = cv::Vec3f(0.0f, 0.0f, 0.0f);
    cv::Vec3f err_hsv_;
};
} // namespace ILLIXR::encoding
