#pragma once

#include <map>
#include <string>

namespace ILLIXR::data_format::units {
/**
 * Enumeration for eyes
 */
enum eyes : int { LEFT_EYE = 0, RIGHT_EYE = 1 };

/**
 * Enumeration for units of distance
 */
enum measurement_unit : int {
    MILLIMETER = 0,
    CENTIMETER = 1,
    METER      = 2,
    INCH       = 3,
    FOOT       = 4,
    PERCENT    = 5,
    PIXEL      = 6,
    UNSET      = 7
};

/**
 * Mapping of `measurement_unit` to a string representation.
 */
const std::map<measurement_unit, const std::string> unit_str{{MILLIMETER, "mm"}, {CENTIMETER, "cm"}, {METER, "m"},
                                                             {INCH, "in"},       {FOOT, "ft"},       {PERCENT, "%"},
                                                             {PIXEL, "px"},      {UNSET, "unitless"}};

constexpr int last_convertable_unit = FOOT;
/**
 * Array of unit conversions, using `measurement_unit` as indices. The first index is the "from" unit and the second
 * index is the "to" unit. For example `conversion_factor[MILLIMETER][INCH]` would yield 0.039, the conversion
 * factor for millimeters to inches.
 */
// mm          cm          m            ft                 in
constexpr float conversion_factor[5][5] = {
    {1.f, 0.1f, .001f, 1.f / (25.4f * 12.f), 1.f / 25.4f},       // mm
    {10.f, 1.f, .01f, 1.f / (2.54f * 12.f), 1.f / 2.54f},        // cm
    {1000.f, 100.f, 1.f, 100.f / (2.54f * 12.f), 100.f / 2.54f}, // m
    {12.f * 25.4f, 12.f * 2.54f, 12.f * .0254f, 1.f, 12.f},      // ft
    {25.4f, 2.54f, .0254f, 1.f / 12.f, 1.f}                    // in
};

/**
 * Convenience function for converting measurements.
 * @param from The `measurement_unit` to convert from
 * @param to The `measurement_unit` to convert to
 * @param val The value to convert
 * @return The converted value
 */
inline float convert(const int from, const int to, float val) {
    return conversion_factor[from][to] * val;
}

inline eyes non_primary(eyes eye) {
    if (eye == LEFT_EYE)
        return RIGHT_EYE;
    return LEFT_EYE;
}

} // namespace ILLIXR::data_format::units
