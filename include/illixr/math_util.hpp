#pragma once
#include "data_format/point.hpp"
#include "data_format/pose.hpp"

#include <cmath>
#ifndef M_PI
    #define M_PI 3.14159265358979323846
#endif
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

/// Calculates a projection matrix with the given tangent angles and clip planes, with reversed depth
void projection_reverse_z(Eigen::Matrix4f* result, const float tan_left, const float tan_right, const float tan_up,
                          float const tan_down, const float near_z, const float far_z) {
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
    (*result)(2, 2) = near_z / (far_z - near_z);
    (*result)(2, 3) = (far_z * near_z) / (far_z - near_z);

    (*result)(3, 0) = 0;
    (*result)(3, 1) = 0;
    (*result)(3, 2) = -1;
    (*result)(3, 3) = 0;
}

/// Calculates a projection matrix with the given FoVs and clip planes
void projection_fov(Eigen::Matrix4f* result, const float fov_left, const float fov_right, const float fov_up,
                    const float fov_down, const float near_z, const float far_z, bool reverse_z = false) {
    const float tan_left  = -tanf(static_cast<float>(fov_left * (M_PI / 180.0f)));
    const float tan_right = tanf(static_cast<float>(fov_right * (M_PI / 180.0f)));

    const float tan_down = -tanf(static_cast<float>(fov_down * (M_PI / 180.0f)));
    const float tan_up   = tanf(static_cast<float>(fov_up * (M_PI / 180.0f)));

    if (reverse_z) {
        projection_reverse_z(result, tan_left, tan_right, tan_up, tan_down, near_z, far_z);
    } else {
        projection(result, tan_left, tan_right, tan_up, tan_down, near_z, far_z);
    }
}

// Expects FoVs in radians
void unreal_projection(Eigen::Matrix4f* result, const float fov_left, const float fov_right, const float fov_up,
                       const float fov_down) {
    // Unreal uses a far plane at infinity and a near plane of 10 centimeters (0.1 meters)
    constexpr float near_z = 0.1;

    const float angle_left  = tanf(static_cast<float>(fov_left));
    const float angle_right = tanf(static_cast<float>(fov_right));
    const float angle_up    = tanf(static_cast<float>(fov_up));
    const float angle_down  = tanf(static_cast<float>(fov_down));

    const float sum_rl = angle_left + angle_right;
    const float sum_tb = angle_up + angle_down;
    const float inv_rl = 1.0f / (angle_right - angle_left);
    const float inv_tb = 1.0f / (angle_up - angle_down);

    (*result)(0, 0) = 2 * inv_rl;
    (*result)(0, 1) = 0;
    (*result)(0, 2) = sum_rl * (-inv_rl);
    (*result)(0, 3) = 0;

    (*result)(1, 0) = 0;
    (*result)(1, 1) = 2 * inv_tb;
    (*result)(1, 2) = sum_tb * (-inv_tb);
    (*result)(1, 3) = 0;

    (*result)(2, 0) = 0;
    (*result)(2, 1) = 0;
    (*result)(2, 2) = 0;
    (*result)(2, 3) = near_z;

    (*result)(3, 0) = 0;
    (*result)(3, 1) = 0;
    (*result)(3, 2) = -1;
    (*result)(3, 3) = 0;
}

// TODO: this is just a complicated version to achieve reverse Z with a finite far plane.
[[maybe_unused]] void godot_projection(Eigen::Matrix4f* result, const float fov_left, const float fov_right, const float fov_up,
                                       const float fov_down) {
    // Godot's default far and near planes are 4000m and 0.05m respectively.
    // https://github.com/godotengine/godot/blob/e96ad5af98547df71b50c4c4695ac348638113e0/modules/openxr/openxr_util.cpp#L97
    // The Vulkan implementation passes in GRAPHICS_OPENGL for some reason.
    constexpr float near_z   = 0.05;
    constexpr float far_z    = 4000;
    constexpr float offset_z = near_z;

    const float angle_left  = tanf(static_cast<float>(fov_left));
    const float angle_right = tanf(static_cast<float>(fov_right));
    const float angle_up    = tanf(static_cast<float>(fov_up));
    const float angle_down  = tanf(static_cast<float>(fov_down));

    const float angle_width  = angle_right - angle_left;
    const float angle_height = angle_up - angle_down;

    Eigen::Matrix4f openxr_matrix;

    openxr_matrix(0, 0) = 2 / angle_width;
    openxr_matrix(0, 1) = 0;
    openxr_matrix(0, 2) = (angle_right + angle_left) / angle_width;
    openxr_matrix(0, 3) = 0;

    openxr_matrix(1, 0) = 0;
    openxr_matrix(1, 1) = 2 / angle_height;
    openxr_matrix(1, 2) = (angle_up + angle_down) / angle_height;
    openxr_matrix(1, 3) = 0;

    openxr_matrix(2, 0) = 0;
    openxr_matrix(2, 1) = 0;
    openxr_matrix(2, 2) = -(far_z + offset_z) / (far_z - near_z);
    openxr_matrix(2, 3) = -(far_z * (near_z + offset_z)) / (far_z - near_z);

    openxr_matrix(3, 0) = 0;
    openxr_matrix(3, 1) = 0;
    openxr_matrix(3, 2) = -1;
    openxr_matrix(3, 3) = 0;

    // Godot then remaps the matrix...
    // https://github.com/Khasehemwy/godot/blob/d950f5f83819240771aebb602bfdd4875363edce/core/math/projection.cpp#L722
    Eigen::Matrix4f remap_z;

    remap_z(0, 0) = 1;
    remap_z(0, 1) = 0;
    remap_z(0, 2) = 0;
    remap_z(0, 3) = 0;

    remap_z(1, 0) = 0;
    remap_z(1, 1) = 1;
    remap_z(1, 2) = 0;
    remap_z(1, 3) = 0;

    remap_z(2, 0) = 0;
    remap_z(2, 1) = 0;
    remap_z(2, 2) = -0.5;
    remap_z(2, 3) = 0.5;

    remap_z(3, 0) = 0;
    remap_z(3, 1) = 0;
    remap_z(3, 2) = 0;
    remap_z(3, 3) = 1;

    (*result) = remap_z * openxr_matrix;
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
        static_cast<float>(sin(rg) * sin(rb) * sin(ra) + cos(rg) * cos(ra)),
        static_cast<float>(sin(rg) * sin(rb) * cos(ra) - cos(rg) * sin(ra)), static_cast<float>(-sin(rb)),
        static_cast<float>(cos(rb) * sin(ra)), static_cast<float>(cos(rb) * cos(ra));
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
