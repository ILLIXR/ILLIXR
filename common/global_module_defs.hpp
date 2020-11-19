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
 * TODO: Support dynamic type casting
 * TODO: Support more than just longs/integers
 * TODO: Support constexpr
 */
#ifndef DECLARE_CONST
#define DECLARE_CONST(_NAME, _TYPE, _FROM_STR, _VALUE) \
static const std::string VAL_STR_##_NAME{ var_from_env(STRINGIFY(_NAME)) }; \
static const _TYPE _NAME{ (VAL_STR_##_NAME.empty()) ? _VALUE : _FROM_STR(VAL_STR_##_NAME) }
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


DECLARE_CONST(DEFAULT_FB_WIDTH,     int,    std::stoi,  2560); // Pixels
DECLARE_CONST(DEFAULT_FB_HEIGHT,    int,    std::stoi,  1440); // Pixels
DECLARE_CONST(DEFAULT_RUN_DURATION, long,   std::stol,  60L ); // Seconds
DECLARE_CONST(DEFAULT_REFRESH_RATE, double, std::stod,  60.0); // Hz

}
