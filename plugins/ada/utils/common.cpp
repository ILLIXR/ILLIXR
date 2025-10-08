#include "common.hpp"

using namespace ILLIXR::encoding;

encoding_common::encoding_common() {
    cv::Mat rgb_mat(1, 1, CV_32FC3, err_rgb_.val);
    cv::Mat hsv_mat;
    rgb2hsv(rgb_mat, hsv_mat);
    err_hsv_ = hsv_mat.at<cv::Vec3f>(0, 0);
}

void encoding_common::quantize(const cv::Mat& input, cv::Mat& output) {
    output.create(input.size(), CV_8UC3);
    input.convertTo(output, CV_8U, QUANT_MAX);
}

void encoding_common::dequantize(const cv::Mat& input, cv::Mat& output) {
    output.create(input.size(), CV_32FC3);
    input.convertTo(output, CV_32F, 1.0 / 255.0);
}

void encoding_common::rgb2hsv(const cv::Mat& rgb, cv::Mat& hsv) {
    hsv.create(rgb.size(), CV_32FC3);

    for (int i = 0; i < rgb.rows; i++) {
        for (int j = 0; j < rgb.cols; j++) {
            const auto& pixel = rgb.at<cv::Vec3f>(i, j);
            float red = pixel[0];
            float green = pixel[1];
            float blue = pixel[2];

            float max_val = std::max({red, green, blue});
            float min_val = std::min({red, green, blue});
            float delta = max_val - min_val;

            float hue = 0.f;
            float saturation = 0.f;
            float value = max_val;

            if (delta > 0) {
                saturation = delta / max_val;

                if (max_val == red) {
                    hue = 0 + (green - blue) / delta;
                } else if (max_val == green) {
                    hue = 2 + (blue - red) / delta;
                } else {
                    hue = 4 + (red - green) / delta;
                }

                hue *= 60.0f;
                if (hue < 0) hue += 360.0f;
            }

            hsv.at<cv::Vec3f>(i, j) = cv::Vec3f(hue, saturation, value);
        }
    }
}

void encoding_common::hsv2rgb(const cv::Mat& hsv, cv::Mat& rgb) {
    rgb.create(hsv.size(), CV_32FC3);

    for (int i = 0; i < hsv.rows; i++) {
        for (int j = 0; j < hsv.cols; j++) {
            const auto& pixel = hsv.at<cv::Vec3f>(i, j);
            float hue = pixel[0];
            float saturation = pixel[1];
            float value = pixel[2];

            hue = hue / 60.0f;
            int hi = static_cast<int>(std::floor(hue)) % 6;
            float f = hue - std::floor(hue);
            float p = value * (1 - saturation);
            float q = value * (1 - saturation * f);
            float t = value * (1 - saturation * (1 - f));

            float red, green, blue;
            switch (hi) {
            case 0:
                red = value;
                green = t;
                blue = p;
                break;
            case 1:
                red = q;
                green = value;
                blue = p;
                break;
            case 2:
                red = p;
                green = value;
                blue = t;
                break;
            case 3:
                red = p;
                green = q;
                blue = value;
                break;
            case 4:
                red = t;
                green = p;
                blue = value;
                break;
            case 5:
                red = value;
                green = p;
                blue = q;
                break;
            default:
                red = green = blue = 0;
                break;
            }

            rgb.at<cv::Vec3f>(i, j) = cv::Vec3f(red, green, blue);
        }
    }
}
