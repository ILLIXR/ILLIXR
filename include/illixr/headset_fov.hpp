#pragma once

#include <array>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace ILLIXR {

struct quest3_params {
    // Quest 3 asymmetric FOV values (from xrLocateViews)
    //
    // Key difference from previous values:
    // - Inner angles (nose side) are ~40deg not ~52deg
    // - Outer angles are ~54deg
    // - Previous symmetric values caused "wall-eyed" effect
    //
    // Left eye:  angleLeft=-54deg, angleRight=+40deg, angleUp=+44deg, angleDown=-55deg
    // Right eye: angleLeft=-40deg, angleRight=+54deg, angleUp=+44deg, angleDown=-55deg

    static constexpr float fov_left[2]  = {-0.94247776f, -0.6981317f}; // Left eye: -54deg, Right eye: -40deg
    static constexpr float fov_right[2] = {0.6981317f, 0.94247776f};   // Left eye: +40deg, Right eye: +54deg
    static constexpr float fov_up[2]    = {0.7679449f, 0.7679449f};    // Both eyes: +44deg
    static constexpr float fov_down[2]  = {-0.9599311f, -0.9599311f};  // Both eyes: -55deg
};

struct index_params {
    static constexpr float fov_left[2]  = {-0.907341f, -0.897566f};
    static constexpr float fov_right[2] = {0.897500f, 0.907700f};
    static constexpr float fov_up[2]    = {0.953644f, 0.954293f};
    static constexpr float fov_down[2]  = {-0.953628f, -0.952802f};
};

// Select before starting the process. Keep Quest 3 as the fallback for existing
// Android launchers; the desktop Index launchers explicitly set ILLIXR_HEADSET.
inline std::string configured_headset() {
    const char* value = std::getenv("ILLIXR_HEADSET");
    const std::string headset = value == nullptr ? "quest3" : value;
    if (headset != "quest3" && headset != "index") {
        throw std::runtime_error("Invalid ILLIXR_HEADSET='" + headset + "'; expected quest3 or index");
    }
    return headset;
}

// Preserve the interface used by the separately built Monado ILLIXR driver.
// These are runtime presets (radians), not compile-time constants. Overscan is
// applied by the consumer, just as it was for the fixed server FOV.
struct server_params {
    inline static const std::string headset = configured_headset();
    inline static const std::array<float, 2> fov_left = {
        headset == "index" ? index_params::fov_left[0] : quest3_params::fov_left[0],
        headset == "index" ? index_params::fov_left[1] : quest3_params::fov_left[1]};
    inline static const std::array<float, 2> fov_right = {
        headset == "index" ? index_params::fov_right[0] : quest3_params::fov_right[0],
        headset == "index" ? index_params::fov_right[1] : quest3_params::fov_right[1]};
    inline static const std::array<float, 2> fov_up = {
        headset == "index" ? index_params::fov_up[0] : quest3_params::fov_up[0],
        headset == "index" ? index_params::fov_up[1] : quest3_params::fov_up[1]};
    inline static const std::array<float, 2> fov_down = {
        headset == "index" ? index_params::fov_down[0] : quest3_params::fov_down[0],
        headset == "index" ? index_params::fov_down[1] : quest3_params::fov_down[1]};
};

} // namespace ILLIXR
