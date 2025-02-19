#pragma once

#include "global_module_defs.hpp"

#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <spdlog/spdlog.h>
#include <string>
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

static const bool ENABLE_VERBOSE_ERRORS{getenv("ILLIXR_ENABLE_VERBOSE_ERRORS") != nullptr &&
                                        ILLIXR::str_to_bool(getenv("ILLIXR_ENABLE_VERBOSE_ERRORS"))};

/**
 * @brief Support function to report errno values when debugging.
 *
 * If errno is set, this function will report errno's value and the calling context.
 * It will subsequently clear errno (reset value to 0).
 * Otherwise, this function does nothing.
 */
inline void report_and_clear_errno([[maybe_unused]] const std::string& file, [[maybe_unused]] const int& line,
                                   [[maybe_unused]] const std::string& function, [[maybe_unused]] const std::string& msg = "") {
    if (errno > 0) {
        if (ILLIXR::ENABLE_VERBOSE_ERRORS) {
            spdlog::get("illixr")->error("[error_util] || Errno was set: {} @ {}:{} [{}]", errno, file, line, function);
            if (!msg.empty()) {
                spdlog::get("illixr")->error("[error_util]|> Message: {}", msg);
            }
        }
        errno = 0;
    }
}

} // namespace ILLIXR
