// Common parameters. Ultimately, these need to be moved to a yaml file.
#pragma once

#include "relative_clock.hpp"

#include <algorithm>
#include <cmath>
#ifndef M_PI
    #define M_PI 3.14159265358979323846
#endif
#include <stdexcept>
#include <string>

namespace ILLIXR {

/// Display parameters
struct display_params {
    // Display width in pixels
    static constexpr unsigned width_pixels = 2880;

    // Display height in pixels
    static constexpr unsigned height_pixels = 1600;

    // Display width in meters
    static constexpr float width_meters = 0.11047f;

    // Display height in meters
    static constexpr float height_meters = 0.06214f;

    // Separation between lens centers in meters
    static constexpr float lens_separation = 0.05;

    // Vertical position of the lens in meters
    [[maybe_unused]] static constexpr float lens_vertical_position = height_meters / 2.0f;

    // Display horizontal field-of-view in degrees
    static constexpr float fov_x = 108.06f;

    // Display vertical field-of-view in degrees
    static constexpr float fov_y = 109.16f;

    // Meters per tangent angle at the center of the HMD (required by timewarp_gl's distortion correction)
    static constexpr float meters_per_tan_angle = width_meters / (2 * (fov_x * M_PI / 180.0f));

    // Inter-pupilary distance (ipd) in meters
    static constexpr float ipd = 0.064f;

    // Display refresh rate in Hz
    static constexpr float frequency = 144.0f;

    // Display period in nanoseconds
    static constexpr duration period = freq_to_period(frequency);

    // Chromatic aberration constants
    static constexpr float aberration[4] = {-0.016f, 0.0f, 0.024f, 0.0f};
};

/// Rendering parameters
struct rendering_params {
    // Near plane distance in meters
    static constexpr float near_z = 0.1f;

    // Far plane distance in meters
    static constexpr float far_z = 20.0f;

    static constexpr bool reverse_z = true;
};

// Offloading parameters - this really should be extended to everything though
constexpr float server_fov = 0.99;

struct server_params {
    // static constexpr float fov_left[2] = {-server_fov, -server_fov};
    // static constexpr float fov_right[2] = {server_fov, server_fov};
    // static constexpr float fov_up[2] = {server_fov, server_fov};
    // static constexpr float fov_down[2] = {-server_fov, -server_fov};
    //
    //    // The server can render at an arbitrary resolution
    //    static constexpr unsigned width_pixels = 2160;
    //    static constexpr unsigned height_pixels = 2400;

    static constexpr float fov_left[2]  = {-0.907341, -0.897566};
    static constexpr float fov_right[2] = {0.897500, 0.907700};
    static constexpr float fov_up[2]    = {0.953644, 0.954293};
    static constexpr float fov_down[2]  = {-0.953628, -0.952802};
};

struct index_params {
    static constexpr float fov_left[2]  = {-0.907341, -0.897566};
    static constexpr float fov_right[2] = {0.897500, 0.907700};
    static constexpr float fov_up[2]    = {0.953644, 0.954293};
    static constexpr float fov_down[2]  = {-0.953628, -0.952802};
};

;

/**
 * @brief Convert a string containing a (python) boolean to the bool type
 */
inline bool str_to_bool(const std::string& var) {
    std::string temp = var;
    std::transform(temp.begin(), temp.end(), temp.begin(), ::toupper);
    if (temp.empty())
        return false;
    return (temp == "TRUE" || temp == "1") ? true
        : (temp == "FALSE")                ? false
                                           : throw std::runtime_error("Invalid conversion from std::string to bool");
}

} // namespace ILLIXR
