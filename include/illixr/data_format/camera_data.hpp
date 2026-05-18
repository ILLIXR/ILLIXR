#pragma once

#include "illixr/data_format/poses/pose_base.hpp"
#include "illixr/switchboard.hpp"

#include <cstddef>
#include <map>
#include <utility>

#ifndef M_PI
    #define M_PI 3.14159265358979323846
#endif
/*
 * The structs in this file were created to hold information about the camera which produced the images. This information
 * includes: center pixels, fields of view, and number of pixels across images. This information is needed by the hand
 * tracking plugin to determine the actual spatial coordinates of the hands.
 */

constexpr double T_M_PI = 2 * M_PI;

namespace ILLIXR::data_format {

/**
 * @brief A data structure to hold relevant camera information. This information is constant (per camera)
 */
struct ccd_data {
    const float  center_x;       //!< center pixel along x axis
    const float  center_y;       //!< center pixel along y axis
    const double vertical_fov;   //!< vertical field of view, in radians
    const double horizontal_fov; //!< horizontal field of view in radians

    ccd_data() = delete;

    /**
     * Constructor
     * @param cx center pixel along x axis
     * @param cy center pixel along y axis
     * @param vf vertical field of view, should be in radians, but will auto-convert
     * @param hf vertical field of view, should be in radians, but will auto-convert
     */
    ccd_data(const float cx, const float cy, const double vf, const double hf)
        : center_x{cx}
        , center_y{cy}
        , vertical_fov{(vf > T_M_PI) ? vf * M_PI / 180. : vf}
        // assume any value below 2PI is in radians, anything else is degrees
        , horizontal_fov{(hf > T_M_PI) ? hf * M_PI / 180. : hf} { }
};

typedef std::map<pose::side, ccd_data>
    ccd_map; //!< mapping of ccd information to the eye it is associated with, for monocular cameras use `ILLIXR::data_format::pose::LEFT`

/**
 * @brief Data structure to hold information about the full camera system. This information is mostly constant.
 */
struct camera_data : switchboard::event {
    size_t                  width;    //!< width of the output image(s) in pixels
    size_t                  height;   //!< height of the output image(s) in pixels
    float                   fps;      //!< frames per second being used
    float                   baseline; //!< distance between left and right eye center pixels
    ccd_map                 ccds;     //!< camera specific information

    camera_data()
        : width{0}
        , height{0}
        , fps{0.}
        , baseline{0.,}
        , ccds{} {}

    camera_data(const size_t width_, const size_t height_, const float fps_, const float baseline_,
                ccd_map ccds_)
        : width{width_}
        , height{height_}
        , fps{fps_}
        , baseline{baseline_}
        , ccds{std::move(ccds_)} { }

    ccd_data operator[](const pose::side idx) const {
        return ccds.at(idx);
    }
};
} // namespace ILLIXR::data_format
