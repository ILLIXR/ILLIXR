#pragma once

#include <array> // for std::array
#include <string> // for std::string
#include <utility> // for std::move
#include <opencv2/core/mat.hpp>
#undef Success // For 'Success' conflict
#include <eigen3/Eigen/Dense>
#include <GL/gl.h>
//#undef Complex // For 'Complex' conflict
#include "phonebook.hpp"
#include "relative_clock.hpp"
#include "switchboard.hpp"

// Tell gldemo and timewarp_gl to use two texture handle for left and right eye
#define USE_ALT_EYE_FORMAT

namespace ILLIXR {
using ullong = unsigned long long;

struct cam_type : switchboard::event {
    [[maybe_unused]] time_point time;
    cv::Mat                     img0;
    cv::Mat                     img1;
};

struct imu_type : switchboard::event {
    [[maybe_unused]] time_point time;
    Eigen::Vector3d             angular_v;
    Eigen::Vector3d             linear_a;
};

struct rgb_depth_type : public switchboard::event {
    [[maybe_unused]] time_point time;
    cv::Mat                     rgb;
    cv::Mat                     depth;
};

// Values needed to initialize the IMU integrator
struct imu_params {
    double                      gyro_noise;
    double                      acc_noise;
    double                      gyro_walk;
    double                      acc_walk;
    Eigen::Matrix<double, 3, 1> n_gravity;
    double                      imu_integration_sigma;
    double                      nominal_rate;
};

// IMU biases, initialization params, and slow pose needed by the IMU integrator
struct imu_integrator_input : public switchboard::event {
    time_point last_cam_integration_time;
    duration   t_offset;
    imu_params params;

    Eigen::Vector3d             biasAcc;
    Eigen::Vector3d             biasGyro;
    Eigen::Matrix<double, 3, 1> position;
    Eigen::Matrix<double, 3, 1> velocity;
    Eigen::Quaterniond          quat;
};

// Output of the IMU integrator to be used by pose prediction
struct imu_raw_type : public switchboard::event {
    // Biases from the last two IMU integration iterations used by RK4 for pose predict
    Eigen::Matrix<double, 3, 1> w_hat;
    Eigen::Matrix<double, 3, 1> a_hat;
    Eigen::Matrix<double, 3, 1> w_hat2;
    Eigen::Matrix<double, 3, 1> a_hat2;

    // Faster pose propagated forwards by the IMU integrator
    Eigen::Matrix<double, 3, 1> pos;
    Eigen::Matrix<double, 3, 1> vel;
    Eigen::Quaterniond          quat;
    time_point                  imu_time;
};

// struct lazy_load_image {
//     lazy_load_image() = default;

//     lazy_load_image(const std::string& path)
//         : _m_path(path) { }

//     cv::Mat load_grayscale() {
//         _m_mat = cv::imread(_m_path, cv::IMREAD_GRAYSCALE);

//         assert(!_m_mat.empty());
        
//         return _m_mat;
//     }

//     cv::Mat load_rgb() {
//         _m_mat = cv::imread(_m_path, cv::IMREAD_COLOR);

//         assert(!_m_mat.empty());
        
//         return _m_mat;
//     }

//     cv::Math load_depth() {
//         _m_mat = cv::imread(_m_path, cv::IMREAD_COLOR);
//         // TODO: how should depth images be read?

//         assert(!_m_mat.empty());
        
//         return _m_mat;
//     }

// private:
//     std::string _m_path;
//     cv::Mat     _m_mat;
// };

struct pose_type : public switchboard::event {
    time_point         sensor_time; // Recorded time of sensor data ingestion
    Eigen::Vector3f    position;
    Eigen::Quaternionf orientation;

    pose_type()
        : sensor_time{time_point{}}
        , position{Eigen::Vector3f{0, 0, 0}}
        , orientation{Eigen::Quaternionf{1, 0, 0, 0}} { }

    pose_type(time_point sensor_time_, Eigen::Vector3f position_, Eigen::Quaternionf orientation_)
        : sensor_time{sensor_time_}
        , position{position_}
        , orientation{orientation_} { }
};

struct  fast_pose_type {
    pose_type  pose;
    time_point predict_computed_time; // Time at which the prediction was computed
    time_point predict_target_time;   // Time that prediction targeted.
};

// Using arrays as a swapchain
// Array of left eyes, array of right eyes
// This more closely matches the format used by Monado
struct rendered_frame : public switchboard::event {
    std::array<GLuint, 2> texture_handles; // Does not change between swaps in swapchain
    std::array<GLuint, 2> swap_indices;    // Which element of the swapchain
    fast_pose_type        render_pose;     // The pose used when rendering this frame.
    time_point            sample_time;
    time_point            render_time;

    rendered_frame() { }

    rendered_frame(std::array<GLuint, 2>&& texture_handles_, std::array<GLuint, 2>&& swap_indices_, fast_pose_type render_pose_,
                   time_point sample_time_, time_point render_time_)
        : texture_handles{std::move(texture_handles_)}
        , swap_indices{std::move(swap_indices_)}
        , render_pose(render_pose_)
        , sample_time(sample_time_)
        , render_time(render_time_) { }
};

struct hologram_input : public switchboard::event {
    uint seq;

    hologram_input() { }

    hologram_input(uint seq_)
        : seq{seq_} { }
};

// High-level HMD specification, timewarp plugin
// may/will calculate additional HMD info based on these specifications
struct hmd_physical_info {
    float ipd;
    int   displayPixelsWide;
    int   displayPixelsHigh;
    float chromaticAberration[4];
    float K[11];
    int   visiblePixelsWide;
    int   visiblePixelsHigh;
    float visibleMetersWide;
    float visibleMetersHigh;
    float lensSeparationInMeters;
    float metersPerTanAngleAtCenter;
};

struct texture_pose : public switchboard::event {
    duration           offload_duration;
    unsigned char*     image;
    time_point         pose_time;
    Eigen::Vector3f    position;
    Eigen::Quaternionf latest_quaternion;
    Eigen::Quaternionf render_quaternion;

    texture_pose() { }

    texture_pose(duration offload_duration_, unsigned char* image_, time_point pose_time_, Eigen::Vector3f position_,
                 Eigen::Quaternionf latest_quaternion_, Eigen::Quaternionf render_quaternion_)
        : offload_duration{offload_duration_}
        , image{image_}
        , pose_time{pose_time_}
        , position{position_}
        , latest_quaternion{latest_quaternion_}
        , render_quaternion{render_quaternion_} { }
};
} // namespace ILLIXR
