#pragma once

#include <cstdint>

namespace ILLIXR::data_format {

/** How a stereo image pair should be presented by an XR compositor. */
enum class stereo_presentation_mode : std::uint8_t {
    /** Submit independent left/right images as an OpenXR projection layer. */
    stereo_fullscreen = 0,
    /** Place one monoscopic image on a world-anchored compositor quad. */
    mono_panel = 1,
    /** Place one monoscopic image on a compositor quad fixed to the viewer. */
    head_locked_panel = 2,
};

} // namespace ILLIXR::data_format
