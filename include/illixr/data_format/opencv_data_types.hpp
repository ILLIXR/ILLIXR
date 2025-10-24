#pragma once

#include "illixr/data_format/unit.hpp"
#include "illixr/switchboard.hpp"

#include <fstream>
#include <map>
#include <opencv2/core/mat.hpp>
#include <utility>

namespace ILLIXR::data_format {
namespace image {
    /**
     * Enumeration of image type
     */
    enum image_type { LEFT_EYE, RIGHT_EYE, RGB, DEPTH, LEFT_EYE_PROCESSED, RIGHT_EYE_PROCESSED, RGB_PROCESSED, CONFIDENCE };

    /**
     * Mapping of `image_type` to a string representation.
     */
    const std::map<image_type, const std::string> image_type_map = {{LEFT_EYE, "LEFT_EYE"},
                                                                    {RIGHT_EYE, "RIGHT_EYE"},
                                                                    {RGB, "RGB"},
                                                                    {DEPTH, "DEPTH"},
                                                                    {LEFT_EYE_PROCESSED, "LEFT_EYE_PROCESSED"},
                                                                    {RIGHT_EYE_PROCESSED, "RIGHT_EYE_PROCESSED"},
                                                                    {RGB_PROCESSED, "RGB_PROCESSED"},
                                                                    {CONFIDENCE, "CONFIDENCE"}};
} // namespace image

/**
 * Enumeration of camera types
 */
namespace camera {
    enum cam_type { BINOCULAR, MONOCULAR, RGB_DEPTH, DEPTH, ZED };
}

/**
 * Base struct for all camera classes
 */
struct cam_base_type : switchboard::event {
    time_point                           time;     //!< Time associated with the image(s)
    camera::cam_type                     type;     //!< What type of camera is this
    std::map<image::image_type, cv::Mat> images{}; //!< Mapping of the images with their types

    cam_base_type(time_point _time, std::map<image::image_type, cv::Mat> imgs, camera::cam_type _type)
        : time(_time)
        , type(_type)
        , images{std::move(imgs)} { }

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

/**
 * For binocular images (left and right eyes).
 */
struct [[maybe_unused]] binocular_cam_type : cam_base_type {
    binocular_cam_type(time_point _time, cv::Mat _img0, cv::Mat _img1)
        : cam_base_type(_time, {{image::LEFT_EYE, _img0}, {image::RIGHT_EYE, _img1}}, camera::BINOCULAR) { }
};

/**
 * For monocular (single) images, like from a webcam.
 */
struct [[maybe_unused]] monocular_cam_type : cam_base_type {
    monocular_cam_type(time_point _time, cv::Mat _img)
        : cam_base_type(_time, {{image::RGB, _img}}, camera::MONOCULAR) { }

    [[nodiscard]] cv::Mat img() const {
        return images.at(image::RGB);
    }
};

/**
 * For RGB based depth images
 */
struct [[maybe_unused]] rgb_depth_type : cam_base_type {
    units::measurement_unit units;

    rgb_depth_type(time_point _time, cv::Mat _rgb, cv::Mat _depth, units::measurement_unit units_ = units::UNSET)
        : cam_base_type(_time, {{image::RGB, _rgb}, {image::DEPTH, _depth}}, camera::RGB_DEPTH)
        , units{units_} { }
};

/**
 * For "grey-scale" depth images
 */
struct [[maybe_unused]] depth_type : cam_base_type {
    units::measurement_unit units;

    depth_type(time_point _time, cv::Mat _depth, units::measurement_unit units_)
        : cam_base_type(_time, {{image::DEPTH, _depth}}, camera::DEPTH)
        , units{units_} { }
};
} // namespace ILLIXR::data_format
