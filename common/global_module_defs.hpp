/*
This is the file where default values are defined
*/

#pragma once

namespace ILLIXR{
	
#ifndef FB_WIDTH
#define FB_WIDTH FB_WIDTH

//Setting default Framebuffer width
static constexpr int FB_WIDTH = 2560; //Pixels
#endif

#ifndef FB_HEIGHT
#define FB_HEIGHT FB_HEIGHT

//Setting default framebuffer height
static constexpr int FB_HEIGHT = 1440; //Pixels
#endif //FB_HEIGHT

/**
 * @brief Convert a string containing a (python) boolean to the bool type
 */
bool str_to_bool(std::string var) {
    return (var == "True")  ? true  :
           (var == "False") ? false :
           throw new std::runtime_error("Invalid conversion from std::string to bool");
}

}
