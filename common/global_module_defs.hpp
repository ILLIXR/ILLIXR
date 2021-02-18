/*
This is the file where default values are defined
*/

#pragma once

#include <cerrno>
#include <string>


/// Parameterless macro for report_and_clear_errno.
#ifndef RAC_ERRNO
#define RAC_ERRNO() report_and_clear_errno(__FILE__, __LINE__, __func__)
#endif /// RAC_ERRNO

/// Parameterized macro for report_and_clear_errno.
/// Prints a message from the calling context for additional info.
#ifndef RAC_ERRNO_MSG
#define RAC_ERRNO_MSG(msg) report_and_clear_errno(__FILE__, __LINE__, __func__, msg)
#endif /// RAC_ERRNO_MSG


/**
 * @brief Support function to debug.
 *
 * If errno is set, this function will report errno's value and the calling context.
 * It will subsequently clear errno (reset value to 0).
 * Otherwise, this function does nothing.
 */
inline void report_and_clear_errno(
    const std::string& file,
    const int& line,
    const std::string function,
    const std::string& msg = ""
) {
#ifndef NDEBUG
    if (errno > 0) {
        std::cerr << "|| Errno was set: " << errno << " @ " << file << ":" << line << "[" << function << "]" << std::endl;
        if (!msg.empty()) {
            std::cerr << "|> Message: " << msg << std::endl;
        }
        errno = 0;
    }
#else /// NDEBUG
    /// Silence unused parameter warning when compiling with opt
    (void) file;
    (void) line;
    (void) function;
    (void) msg;
#endif /// NDEBUG
}


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
           throw std::runtime_error("Invalid conversion from std::string to bool");
}

/// Temporary environment variable getter. Not needed once #198 is merged.
std::string getenv_or(std::string var, std::string default_) {
    if (std::getenv(var.c_str())) {
        return {std::getenv(var.c_str())};
    } else {
        return default_;
    }
}

}
