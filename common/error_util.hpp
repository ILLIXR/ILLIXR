#pragma once

#include <cerrno>
#include <cstdlib>
#include <string>
#include <iostream>

#include "global_module_defs.hpp"


/**
 * @brief Parameterless macro for report_and_clear_errno.
 */
#ifndef RAC_ERRNO
#define RAC_ERRNO() ILLIXR::report_and_clear_errno(__FILE__, __LINE__, __func__)
#endif /// RAC_ERRNO

/**
 * @brief Parameterized macro for report_and_clear_errno.
 *
 * Prints a message from the calling context for additional info.
 */
#ifndef RAC_ERRNO_MSG
#define RAC_ERRNO_MSG(msg) ILLIXR::report_and_clear_errno(__FILE__, __LINE__, __func__, msg)
#endif /// RAC_ERRNO_MSG


namespace ILLIXR {

static const bool ENABLE_VERBOSE_ERRORS {ILLIXR::str_to_bool(ILLIXR::getenv_or("ILLIXR_ENABLE_VERBOSE_ERRORS", "False"))};

/**
 * @brief Support function to report errno values when debugging (NDEBUG).
 *
 * If errno is set, this function will report errno's value and the calling context.
 * It will subsequently clear errno (reset value to 0).
 * Otherwise, this function does nothing.
 */
inline void report_and_clear_errno(
    [[maybe_unused]] const std::string& file,
    [[maybe_unused]] const int& line,
    [[maybe_unused]] const std::string& function,
    [[maybe_unused]] const std::string& msg = ""
) {
#ifndef NDEBUG
    if (errno > 0) {
        if (ILLIXR::ENABLE_VERBOSE_ERRORS) {
            std::cerr << "|| Errno was set: " << errno << " @ " << file << ":" << line << "[" << function << "]" << std::endl;
            if (!msg.empty()) {
                std::cerr << "|> Message: " << msg << std::endl;
            }
        }
        errno = 0;
    }
#endif /// NDEBUG
}


/**
 * @brief Exits the application during a fatal error.
 *
 * Switches to using abort during debugging over std::exit so that we can capture
 * SIGABRT for debugging.
 */
inline void abort(const std::string& msg = "", [[maybe_unused]] const int error_val = 1) {
    std::cerr << "** ERROR ** " << msg << std::endl;
#ifndef NDEBUG
    std::abort();
#else /// NDEBUG
    std::exit(error_val);
#endif /// NDEBUG
}

} /// namespace ILLIXR
