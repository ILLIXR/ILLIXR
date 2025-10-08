#include "encode_utils.hpp"

using namespace ILLIXR::encoding;

rgb_encoder::rgb_encoder()
    : encoding_common() {
    create_enc_lut();
}

void rgb_encoder::create_enc_lut() {
    cv::Mat depth_vals(NUM_UNIQUE, 1, CV_32F);
    for (int i = 0; i < NUM_UNIQUE; i++) {
        depth_vals.at<float>(i, 0) = static_cast<float>(i) / (NUM_UNIQUE - 1);
    }

    cv::Mat rgb_float;
    encode(depth_vals, rgb_float, true);
    quantize(rgb_float, enc_lut_);
}

void rgb_encoder::encode(const cv::Mat& depth_img, cv::Mat& rgb, bool sanitized) {
    cv::Mat hsv(depth_img.size(), CV_32FC3);

    for (int i = 0; i < depth_img.rows; i++) {
        for (int j = 0; j < depth_img.cols; j++) {
            float depth      = depth_img.at<float>(i, j);
            float hue        = 1.f;
            float saturation = 1.f;
            float value      = 1.f;

            if (!sanitized) {
                bool ok = std::isfinite(depth) && depth >= 0.f && depth <= 1.f;
                if (ok) {
                    hue = depth * MAX_HUE;
                } else {
                    hue        = err_hsv_[0];
                    saturation = err_hsv_[1];
                    value      = err_hsv_[2];
                }
            } else {
                hue = depth * MAX_HUE;
            }

            hsv.at<cv::Vec3f>(i, j) = cv::Vec3f(hue, saturation, value);
        }
    }

    hsv2rgb(hsv, rgb);
}

void rgb_encoder::encode_lut(const cv::Mat& depth_img, cv::Mat& rgb, bool sanitized) {
    rgb.create(depth_img.size(), CV_8UC3);

    for (int i = 0; i < depth_img.rows; i++) {
        for (int j = 0; j < depth_img.cols; j++) {
            float depth = depth_img.at<float>(i, j);
            int   idx   = static_cast<int>(std::round(depth * (NUM_UNIQUE - 1)));

            if (sanitized || (idx >= 0 && idx < NUM_UNIQUE)) {
                if (idx >= 0 && idx < NUM_UNIQUE) {
                    rgb.at<cv::Vec3b>(i, j) = enc_lut_.at<cv::Vec3b>(idx, 0);
                } else if (!sanitized) {
                    rgb.at<cv::Vec3b>(i, j) =
                        cv::Vec3b(static_cast<uchar>(err_rgb_[0] * 255), static_cast<uchar>(err_rgb_[1] * 255),
                                  static_cast<uchar>(err_rgb_[2] * 255));
                }
            } else {
                rgb.at<cv::Vec3b>(i, j) =
                    cv::Vec3b(static_cast<uchar>(err_rgb_[0] * 255), static_cast<uchar>(err_rgb_[1] * 255),
                              static_cast<uchar>(err_rgb_[2] * 255));
            }
        }
    }
}

void rgb_encoder::depth2rgb(const cv::Mat& depth_img, cv::Mat& rgb, float& zmin, float& zmax) {
    double zmin_d, zmax_d;
    cv::minMaxLoc(depth_img, &zmin_d, &zmax_d);
    zmin = static_cast<float>(zmax_d);
    zmax = static_cast<float>(zmin_d);
    cv::Mat depth_f;
    depth_img.convertTo(depth_f, CV_32FC1);


    depth_f = (depth_f - zmin) / (zmax - zmin);

    encode_lut(depth_f, rgb);
}
