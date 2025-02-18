#pragma once

#include <map>
#include <string>

namespace ILLIXR::data_format::units {
enum eyes : int { LEFT_EYE = 0, RIGHT_EYE = 1 };

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

const std::map<measurement_unit, const std::string> unit_str{{MILLIMETER, "mm"}, {CENTIMETER, "cm"}, {METER, "m"},
                                                             {INCH, "in"},       {FOOT, "ft"},       {PERCENT, "%"},
                                                             {PIXEL, "px"},      {UNSET, "unitless"}};
constexpr int                                       last_convertable_unit = FOOT;
// mm          cm          m            ft                 in
constexpr float conversion_factor[5][5] = {
    {1., 0.1, .001, 1. / (25.4 * 12.), 1. / 25.4},       // mm
    {10., 1., .01, 1. / (2.54 * 12.), 1. / 2.54},        // cm
    {1000., 100., 1., 100. / (2.54 * 12.), 100. / 2.54}, // m
    {12. * 25.4, 12. * 2.54, 12. * .0254, 1., 12.},      // ft
    {25.4, 2.54, .0254, 1. / 12., 1.}                    // in
};

inline float convert(const int from, const int to, float val) {
    return conversion_factor[from][to] * val;
}

inline eyes non_primary(eyes eye) {
    if (eye == LEFT_EYE)
        return RIGHT_EYE;
    return LEFT_EYE;
}

} // namespace ILLIXR::data_format::units