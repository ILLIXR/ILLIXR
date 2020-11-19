#pragma once

#include <cstdlib>
#include <string>
#include <stdexcept>


namespace ILLIXR
{

/**
 * (Macro expansion) Function used to create string literals from variable names
 */
#ifndef STRINGIFY
#define STRINGIFY_HELP(X) #X
#define STRINGIFY(X) STRINGIFY_HELP(X)
#endif // STRINGIFY

/**
 * (Macro expansion) Function used to declare constants used by modules
 *
 * _NAME:       The constant's name to be used by module code
 * _TYPE:       The type name for the constant
 * _FROM_STR:   The conversion function from a std::string to a value of type _TYPE
 * _VALUE:      The default value for the constant
 *
 * This declaration provides two constant definitions given a variable name "FOO":
 *   1. A const FOO of type _TYPE which is initialized to the default value if
 *      the environment variable "FOO" does not exist or is an empty string
 *   2. A constexpr DEFAULT_FOO of type _TYPE which is always initialized to the
 *      default value provided
 *
 * TODO: Support dynamic type casting
 * TODO: Support constexpr
 */
#ifndef DECLARE_CONST
#define DECLARE_CONST(_NAME, _TYPE, _FROM_STR, _VALUE) \
static const std::string VAL_STR_##_NAME{ var_from_env(STRINGIFY(_NAME)) }; \
static const _TYPE _NAME{ (VAL_STR_##_NAME.empty()) ? _VALUE : _FROM_STR(VAL_STR_##_NAME) }; \
static constexpr _TYPE DEFAULT_##_NAME{ _VALUE }
#endif // DECLARE_CONST


std::string var_from_env(const char * const var_name) noexcept
{
    char* const c_str{ std::getenv(var_name) };
    return (c_str == nullptr) ? std::string{""} : std::string{c_str};
}


/**
 * @brief Convert a string containing a (python) boolean to the bool type
 */
inline bool str_to_bool(std::string var) {
    return (var == "True")  ? true  :
           (var == "False") ? false :
           throw std::runtime_error("Invalid conversion from std::string to bool");
}


DECLARE_CONST(FB_WIDTH,     int,    std::stoi,  2560); // Pixels
DECLARE_CONST(FB_HEIGHT,    int,    std::stoi,  1440); // Pixels
DECLARE_CONST(RUN_DURATION, long,   std::stol,  60L ); // Seconds
DECLARE_CONST(REFRESH_RATE, double, std::stod,  60.0); // Hz

}
