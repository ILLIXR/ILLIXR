/**
 * @brief Unit tests for math_util (include/illixr/math_util.hpp).
 *
 * Expected values were derived by hand from the same perspective-projection formula
 * math_util.hpp implements, using symmetric tangent angles chosen so the arithmetic works out
 * exactly, then verified numerically before being written in as constants below.
 */

#include "illixr/math_util.hpp"

#include <gtest/gtest.h>

namespace ILLIXR {

namespace {

constexpr float kEpsilon = 1e-4f;

TEST(MathUtilTest, ProjectionSymmetricFrustum) {
    // tan_left=-1, tan_right=1, tan_up=1, tan_down=-1, near=1, far=10.
    Eigen::Matrix4f result;
    math_util::projection(&result, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, 10.0f);

    EXPECT_NEAR(result(0, 0), 1.0f, kEpsilon);
    EXPECT_NEAR(result(0, 2), 0.0f, kEpsilon);
    EXPECT_NEAR(result(1, 1), 1.0f, kEpsilon);
    EXPECT_NEAR(result(1, 2), 0.0f, kEpsilon);
    EXPECT_NEAR(result(2, 2), -10.0f / 9.0f, kEpsilon);
    EXPECT_NEAR(result(2, 3), -10.0f / 9.0f, kEpsilon);
    EXPECT_NEAR(result(3, 2), -1.0f, kEpsilon);
    EXPECT_NEAR(result(3, 3), 0.0f, kEpsilon);
}

TEST(MathUtilTest, ProjectionReverseZSymmetricFrustum) {
    Eigen::Matrix4f result;
    math_util::projection_reverse_z(&result, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, 10.0f);

    EXPECT_NEAR(result(2, 2), 1.0f / 9.0f, kEpsilon);
    EXPECT_NEAR(result(2, 3), 10.0f / 9.0f, kEpsilon);
    EXPECT_NEAR(result(3, 2), -1.0f, kEpsilon);
}

TEST(MathUtilTest, ProjectionFovMatchesEquivalentTangentProjection) {
    // 45-degree symmetric FOV -> tan(45 deg) == 1, matching ProjectionSymmetricFrustum's inputs.
    Eigen::Matrix4f from_fov;
    math_util::projection_fov(&from_fov, 45.0f, 45.0f, 45.0f, 45.0f, 1.0f, 10.0f, /*reverse_z=*/false);

    Eigen::Matrix4f from_tan;
    math_util::projection(&from_tan, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, 10.0f);

    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            EXPECT_NEAR(from_fov(row, col), from_tan(row, col), kEpsilon);
        }
    }
}

TEST(MathUtilTest, ProjectionFovReverseZDelegatesCorrectly) {
    Eigen::Matrix4f from_fov;
    math_util::projection_fov(&from_fov, 45.0f, 45.0f, 45.0f, 45.0f, 1.0f, 10.0f, /*reverse_z=*/true);

    Eigen::Matrix4f from_tan;
    math_util::projection_reverse_z(&from_tan, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, 10.0f);

    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            EXPECT_NEAR(from_fov(row, col), from_tan(row, col), kEpsilon);
        }
    }
}

TEST(MathUtilTest, RotationZeroAnglesIsIdentity) {
    Eigen::Matrix3f rot      = math_util::rotation(0.0f, 0.0f, 0.0f);
    Eigen::Matrix3f identity = Eigen::Matrix3f::Identity();
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            EXPECT_NEAR(rot(row, col), identity(row, col), kEpsilon);
        }
    }
}

TEST(MathUtilTest, Rotation90DegreesAboutAlpha) {
    // rotation(90, 0, 0), hand-derived and numerically re-checked: [[1,0,0],[0,0,-1],[0,1,0]]
    Eigen::Matrix3f rot = math_util::rotation(90.0f, 0.0f, 0.0f);
    EXPECT_NEAR(rot(0, 0), 1.0f, kEpsilon);
    EXPECT_NEAR(rot(0, 1), 0.0f, kEpsilon);
    EXPECT_NEAR(rot(0, 2), 0.0f, kEpsilon);
    EXPECT_NEAR(rot(1, 0), 0.0f, kEpsilon);
    EXPECT_NEAR(rot(1, 1), 0.0f, kEpsilon);
    EXPECT_NEAR(rot(1, 2), -1.0f, kEpsilon);
    EXPECT_NEAR(rot(2, 0), 0.0f, kEpsilon);
    EXPECT_NEAR(rot(2, 1), 1.0f, kEpsilon);
    EXPECT_NEAR(rot(2, 2), 0.0f, kEpsilon);
}

TEST(MathUtilTest, ConversionMatrixDiagonalIsIdentity) {
    // conversion[i][i] (a coordinate system converted to itself) should always be the identity.
    for (int i = 0; i < 6; ++i) {
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) {
                EXPECT_NEAR(math_util::conversion[i][i](row, col), math_util::identity(row, col), kEpsilon);
            }
        }
    }
}

} // namespace

} // namespace ILLIXR
