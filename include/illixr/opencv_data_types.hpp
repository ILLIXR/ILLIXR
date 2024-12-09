#pragma once

#include <map>

#include "data_format.hpp"
#include "switchboard.hpp"
#include <fstream>
#include <opencv2/core/mat.hpp>
#include <utility>

namespace ILLIXR {
namespace image {
    enum image_type { LEFT_EYE,
                      RIGHT_EYE,
                      RGB,
                      DEPTH,
                      LEFT_EYE_PROCESSED,
                      RIGHT_EYE_PROCESSED,
                      RGB_PROCESSED,
                      CONFIDENCE};
}
namespace camera {
    enum cam_type { BINOCULAR,
                    MONOCULAR,
                    RGB_DEPTH,
                    DEPTH,
                    ZED };
}

struct cam_base_type : switchboard::event {
    time_point time;
    camera::cam_type type;
    std::map<image::image_type, cv::Mat> images{};

    cam_base_type(time_point _time, std::map<image::image_type, cv::Mat> imgs, camera::cam_type _type)
        : time(_time)
        , type(_type)
        , images{std::move(imgs)} {}

     cv::Mat& operator[](image::image_type idx) {
         return images.at(idx);
     }

     int format(image::image_type idx) {
         return images.at(idx).type();
     }

     [[nodiscard]] size_t size() const {
         return images.size();
     }

     [[nodiscard]] cv::Mat at(const image::image_type idx) const {
         return images.at(idx);
     }

     [[nodiscard]] std::map<image::image_type, cv::Mat>::const_iterator find(const image::image_type idx) const {
         return images.find(idx);
     }

     [[nodiscard]] std::map<image::image_type, cv::Mat>::const_iterator begin() const {
         return images.begin();
     }

     [[nodiscard]] std::map<image::image_type, cv::Mat>::const_iterator end() const {
         return images.end();
     }
};

struct [[maybe_unused]] binocular_cam_type : cam_base_type {
    binocular_cam_type(time_point _time, cv::Mat _img0, cv::Mat _img1)
        : cam_base_type(_time, {{image::LEFT_EYE, _img0}, {image::RIGHT_EYE, _img1}}, camera::BINOCULAR) { }
};

struct [[maybe_unused]] monocular_cam_type : cam_base_type {
    monocular_cam_type(time_point _time, cv::Mat _img)
        : cam_base_type(_time, {{image::RGB, _img}}, camera::MONOCULAR) {}

    [[nodiscard]] cv::Mat img() const {
        return images.at(image::RGB);
    }
};

struct [[maybe_unused]] rgb_depth_type : cam_base_type {
    units::measurement_unit units;
    rgb_depth_type(time_point _time, cv::Mat _rgb, cv::Mat _depth, units::measurement_unit units_ = units::UNSET)
        : cam_base_type(_time, {{image::RGB, _rgb}, {image::DEPTH, _depth}}, camera::RGB_DEPTH)
        , units{units_} {}
};

struct [[maybe_unused]] depth_type : cam_base_type {
    units::measurement_unit units;
    depth_type(time_point _time, cv::Mat _depth, units::measurement_unit units_)
        : cam_base_type(_time, {{image::DEPTH, _depth}}, camera::DEPTH)
        , units{units_} {}
};
} // namespace ILLIXR