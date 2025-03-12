#pragma once

namespace ILLIXR::data_format::coordinates {
/**
 * Enumeration of the possible reference frames
 */
enum frame {
    IMAGE,
    LEFT_HANDED_Y_UP,
    LEFT_HANDED_Z_UP,
    RIGHT_HANDED_Y_UP, // XR_REFERENCE_SPACE_TYPE_VIEW
    RIGHT_HANDED_Z_UP,
    RIGHT_HANDED_Z_UP_X_FWD
};

enum reference_space { VIEWER, WORLD, ROOM = WORLD };

} // namespace ILLIXR::data_format::coordinates
