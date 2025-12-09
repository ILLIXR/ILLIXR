#include "decode_utils.hpp"

using namespace ILLIXR::encoding;

rgb_decoder::rgb_decoder()
    : encoding_common() {
    create_dec_lut();
}

void rgb_decoder::create_dec_lut() {
    // Simplified version - full 3D LUT would be 256x256x256
    // For practical purposes, create a 2D lookup based on quantized encoding
    dec_lut_.create(256, 256 * 256, CV_32F);

    for (int r = 0; r < 256; r++) {
        for (int g = 0; g < 256; g++) {
            for (int b = 0; b < 256; b++) {
                cv::Mat rgb_pixel(1, 1, CV_8UC3, cv::Vec3b(r, g, b));
                cv::Mat depth_val;
                decode(rgb_pixel, depth_val);
                dec_lut_.at<float>(r, g * 256 + b) = depth_val.at<float>(0, 0);
            }
        }
    }
}

void rgb_decoder::decode(const cv::Mat& rgb, cv::Mat& depth_img) {
    cv::Mat rgb_float;
    if (rgb.type() == CV_8UC3) {
        dequantize(rgb, rgb_float);
    } else {
        rgb_float = rgb;
    }

    cv::Mat hsv;
    rgb2hsv(rgb_float, hsv);

    depth_img.create(rgb.size(), CV_32F);
    depth_img.setTo(err_depth_);

    for (int i = 0; i < hsv.rows; i++) {
        for (int j = 0; j < hsv.cols; j++) {
            cv::Vec3f pixel = hsv.at<cv::Vec3f>(i, j);

            if (pixel[1] >= min_saturation_ && pixel[2] >= min_value_ && pixel[0] <= MAX_HUE)
                depth_img.at<float>(i, j) = pixel[0] / MAX_HUE;
        }
    }
}

void rgb_decoder::decode_lut(const cv::Mat& rgb, cv::Mat& depth_img) {
    depth_img.create(rgb.size(), CV_32F);

    for (int i = 0; i < rgb.rows; i++) {
        for (int j = 0; j < rgb.cols; j++) {
            const auto& pixel         = rgb.at<cv::Vec3b>(i, j);
            depth_img.at<float>(i, j) = dec_lut_.at<float>(pixel[0], pixel[1] * 256 + pixel[2]);
        }
    }
}

void rgb_decoder::rgb2depth(const cv::Mat& rgb, cv::Mat& depth, float zmin, float zmax) {
    decode_lut(rgb, depth);

    depth = depth * (zmax - zmin) + zmin;
}
