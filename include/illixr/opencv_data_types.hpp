#pragma once

#include <map>

#include "switchboard.hpp"

#include <opencv2/core/mat.hpp>

namespace ILLIXR {

enum image_type {
    LEFT,
    RIGHT,
    RGB,
    DEPTH
};

enum cam_type {
    BINOCULAR,
    MONOCULAR,
    RGB_DEPTH,
    ZED
};

struct cam_base_type : switchboard::event {
    time_point time;
    cam_type type;
    std::map<image_type, cv::Mat> images{};

    cam_base_type(time_point _time, std::map<image_type, cv::Mat> imgs, cam_type _type)
        : time(_time)
        , type(_type)
        , images{std::move(imgs)} {}

     cv::Mat& operator[](image_type idx) {
         return images.at(idx);
     }

     int format(image_type idx) {
         return images.at(idx).type();
     }

     [[nodiscard]] size_t size() const {
         return images.size();
     }

     [[nodiscard]] cv::Mat at(const image_type idx) const {
         return images.at(idx);
     }

     [[nodiscard]] std::map<image_type, cv::Mat>::const_iterator find(const image_type idx) const {
         return images.find(idx);
     }
};

struct binocular_cam_type : cam_base_type {
    binocular_cam_type(time_point _time, cv::Mat _img0, cv::Mat _img1)
        : cam_base_type(_time, {{LEFT, _img0}, {RIGHT, _img1}}, BINOCULAR) { }
};

struct monocular_cam_type : cam_base_type {
    monocular_cam_type(time_point _time, cv::Mat _img)
        : cam_base_type(_time, {{RGB, _img}}, MONOCULAR) {}

    cv::Mat img() const {
        return images.at(RGB);
    }
};

struct rgb_depth_type : cam_base_type {
    rgb_depth_type(time_point _time, cv::Mat _rgb, cv::Mat _depth)
        : cam_base_type(_time, {{RGB, _rgb}, {DEPTH, _depth}}, RGB_DEPTH) {}
};

struct cam_type_zed : cam_base_type {
    std::size_t serial_no;

    cam_type_zed(time_point _time, cv::Mat _img0, cv::Mat _img1, cv::Mat _rgb, cv::Mat _depth, std::size_t _serial_no)
        : cam_base_type(_time, {{LEFT, _img0}, {RIGHT, _img1}, {RGB, _rgb}, {DEPTH, _depth}}, ZED)
        , serial_no(_serial_no) {}
};
} // namespace ILLIXR