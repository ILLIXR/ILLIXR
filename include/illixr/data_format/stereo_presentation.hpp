#pragma once

#include <cstdint>

namespace ILLIXR::data_format {

/** How a stereo image pair should be presented by an XR compositor. */
enum class stereo_presentation_mode : std::uint8_t {
    stereo_fullscreen = 0,
    mono_panel        = 1,
    head_locked_panel = 2,
};

} // namespace ILLIXR::data_format
