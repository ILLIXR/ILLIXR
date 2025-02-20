#pragma once
#ifndef _USE_MATH_DEFINES
    #define _USE_MATH_DEFINES
#endif
#include "data_format/point.hpp"
#include "data_format/pose.hpp"

#include <cmath>
#include <eigen3/Eigen/Core>

namespace ILLIXR::math_util {
/// Calculates a projection matrix with the given tangent angles and clip planes
void projection(Eigen::Matrix4f* result, const float tan_left, const float tan_right, const float tan_up, float const tan_down,
                const float near_z, const float far_z) {
    const float tan_width  = tan_right - tan_left;
    const float tan_height = tan_up - tan_down;

    // https://www.scratchapixel.com/lessons/3d-basic-rendering/perspective-and-orthographic-projection-matrix/building-basic-perspective-projection-matrix
    (*result)(0, 0) = 2 / tan_width;
    (*result)(0, 1) = 0;
    (*result)(0, 2) = (tan_right + tan_left) / tan_width;
    (*result)(0, 3) = 0;

    (*result)(1, 0) = 0;
    (*result)(1, 1) = 2 / tan_height;
    (*result)(1, 2) = (tan_up + tan_down) / tan_height;
    (*result)(1, 3) = 0;

    (*result)(2, 0) = 0;
    (*result)(2, 1) = 0;
    (*result)(2, 2) = -far_z / (far_z - near_z);
    (*result)(2, 3) = -(far_z * near_z) / (far_z - near_z);

    (*result)(3, 0) = 0;
    (*result)(3, 1) = 0;
    (*result)(3, 2) = -1;
    (*result)(3, 3) = 0;
}

/// Calculates a projection matrix with the given FoVs and clip planes
void projection_fov(Eigen::Matrix4f* result, const float fov_left, const float fov_right, const float fov_up,
                    const float fov_down, const float near_z, const float far_z) {
    const float tan_left  = -tanf(static_cast<float>(fov_left * (M_PI / 180.0f)));
    const float tan_right = tanf(static_cast<float>(fov_right * (M_PI / 180.0f)));

    const float tan_down = -tanf(static_cast<float>(fov_down * (M_PI / 180.0f)));
    const float tan_up   = tanf(static_cast<float>(fov_up * (M_PI / 180.0f)));

    projection(result, tan_left, tan_right, tan_up, tan_down, near_z, far_z);
}

/*
 * Rotation matrix to convert a point from one coordinate system to another, e.g. left hand y up to right hand y up
 *
 */
inline Eigen::Matrix3f rotation(const float alpha, const float beta, const float gamma) {
    Eigen::Matrix3f rot;
    double          ra = alpha * M_PI / 180.;
    double          rb = beta * M_PI / 180.;
    double          rg = gamma * M_PI / 180;
    rot << static_cast<float>(cos(rg) * cos(rb)), static_cast<float>(cos(rg) * sin(rb) * sin(ra) - sin(rg) * cos(ra)),
            static_cast<float>(cos(rg) * sin(rb) * cos(ra) + sin(rg) * sin(ra)), static_cast<float>(sin(rg) * cos(rb)),
            static_cast<float>(sin(rg) * sin(rb) * sin(ra) + cos(rg) * cos(ra)), static_cast<float>(sin(rg) * sin(rb) * cos(ra) - cos(rg) * sin(ra)),
            static_cast<float>(-sin(rb)), static_cast<float>(cos(rb) * sin(ra)), static_cast<float>(cos(rb) * cos(ra));
    return rot;
}

const Eigen::Matrix3f invert_x = (Eigen::Matrix3f() << -1., 0., 0., 0., 1., 0., 0., 0., 1.).finished();
const Eigen::Matrix3f invert_y = (Eigen::Matrix3f() << 1., 0., 0., 0., -1., 0., 0., 0., 1.).finished();
const Eigen::Matrix3f invert_z = (Eigen::Matrix3f() << 1., 0., 0., 0., 1., 0., 0., 0., -1.).finished();
const Eigen::Matrix3f identity = Eigen::Matrix3f::Identity();

// from:      IM (image)                        LHYU (left hand y up)              RHYU (right hand y up)             RHZU
// (right hand z up LHZU (left hand z up)               RHZUXF (right hand z up x forward)                 to:
const Eigen::Matrix3f conversion[6][6] = {
    {identity, invert_y, rotation(180., 0., 0.), rotation(-90., 0., 0.), rotation(-90., 0., 90.) * invert_z,
     rotation(-90., 0., -90.)}, // IM
    {invert_y, identity, invert_z, rotation(-90., 0., 0.) * invert_y, rotation(90., 0., 90.),
     rotation(-90., 0., -90.) * invert_y}, // LHYU
    {rotation(180., 0., 0.), invert_z, identity, rotation(90., 0., 0.), rotation(90., 0., 90.) * invert_z,
     rotation(-90., 0., 90.) * invert_x* invert_y}, // RHYU
    {rotation(90., 0., 0.), invert_y* rotation(90., 0., 0.), rotation(-90., 0., 0.), identity, rotation(0., 0., 90.) * invert_y,
     rotation(0., 0., -90.)}, // RHZU
    {rotation(90., -90., 0.) * invert_y, rotation(-90., -90., 0.), rotation(0., 90., 90.) * invert_y,
     invert_x* rotation(0, 0., 90), identity, invert_y}, // LHZU
    {rotation(90., -90., 0.), rotation(-90., -90., 0.) * invert_y, rotation(0., 90., 90.), rotation(0, 0., 90), invert_y,
     identity}}; // RHZUXF

} // namespace ILLIXR::math_util
