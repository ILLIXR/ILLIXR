#pragma once

#undef Success // For 'Success' conflict
#include <eigen3/Eigen/Dense>
#include <GL/gl.h>
#include <utility>
// #undef Complex // For 'Complex' conflict

#include "relative_clock.hpp"
#include "switchboard.hpp"

// Tell gldemo and timewarp_gl to use two texture handle for left and right eye
#define USE_ALT_EYE_FORMAT

namespace ILLIXR {
using ullong = unsigned long long;

struct imu_type : switchboard::event {
    time_point      time;
    Eigen::Vector3d angular_v;
    Eigen::Vector3d linear_a;

    imu_type(time_point time_, Eigen::Vector3d angular_v_, Eigen::Vector3d linear_a_)
        : time{time_}
        , angular_v{std::move(angular_v_)}
        , linear_a{std::move(linear_a_)} { }
};

struct connection_signal : public switchboard::event {
    bool start;

    connection_signal(bool start_)
        : start{start_} { }
};

// Values needed to initialize the IMU integrator
typedef struct {
    double                      gyro_noise;
    double                      acc_noise;
    double                      gyro_walk;
    double                      acc_walk;
    Eigen::Matrix<double, 3, 1> n_gravity;
    double                      imu_integration_sigma;
    double                      nominal_rate;
} imu_params;

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

    imu_integrator_input(time_point last_cam_integration_time_, duration t_offset_, imu_params params_,
                         Eigen::Vector3d biasAcc_, Eigen::Vector3d biasGyro_, Eigen::Matrix<double, 3, 1> position_,
                         Eigen::Matrix<double, 3, 1> velocity_, Eigen::Quaterniond quat_)
        : last_cam_integration_time{last_cam_integration_time_}
        , t_offset{t_offset_}
        , params{std::move(params_)}
        , biasAcc{std::move(biasAcc_)}
        , biasGyro{std::move(biasGyro_)}
        , position{std::move(position_)}
        , velocity{std::move(velocity_)}
        , quat{std::move(quat_)} { }
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

    imu_raw_type(Eigen::Matrix<double, 3, 1> w_hat_, Eigen::Matrix<double, 3, 1> a_hat_, Eigen::Matrix<double, 3, 1> w_hat2_,
                 Eigen::Matrix<double, 3, 1> a_hat2_, Eigen::Matrix<double, 3, 1> pos_, Eigen::Matrix<double, 3, 1> vel_,
                 Eigen::Quaterniond quat_, time_point imu_time_)
        : w_hat{std::move(w_hat_)}
        , a_hat{std::move(a_hat_)}
        , w_hat2{std::move(w_hat2_)}
        , a_hat2{std::move(a_hat2_)}
        , pos{std::move(pos_)}
        , vel{std::move(vel_)}
        , quat{std::move(quat_)}
        , imu_time{imu_time_} { }
};

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
        , position{std::move(position_)}
        , orientation{std::move(orientation_)} { }
};

typedef struct {
    pose_type  pose;
    time_point predict_computed_time; // Time at which the prediction was computed
    time_point predict_target_time;   // Time that prediction targeted.
} fast_pose_type;

// Used to identify which graphics API is being used (for swapchain construction)
enum class graphics_api { OPENGL, VULKAN, TBD };

// Used to distinguish between different image handles
enum class swapchain_usage { LEFT_SWAPCHAIN, RIGHT_SWAPCHAIN, LEFT_RENDER, RIGHT_RENDER, NA };

typedef struct vk_image_handle {
    int      file_descriptor;
    int64_t  format;
    size_t   allocation_size;
    uint32_t width;
    uint32_t height;

    vk_image_handle(int fd_, int64_t format_, size_t alloc_size, uint32_t width_, uint32_t height_)
        : file_descriptor{fd_}
        , format{format_}
        , allocation_size{alloc_size}
        , width{width_}
        , height{height_} { }
} vk_image_handle;

// This is used to share swapchain images between ILLIXR and Monado.
// When Monado uses its GL pipeline, it's enough to just share a context during creation.
// Otherwise, file descriptors are needed to share the images.
struct image_handle : public switchboard::event {
    graphics_api type;

    union {
        GLuint          gl_handle;
        vk_image_handle vk_handle;
    };

    uint32_t        num_images;
    swapchain_usage usage;

    image_handle()
        : type{graphics_api::TBD}
        , gl_handle{0}
        , num_images{0}
        , usage{swapchain_usage::NA} { }

    image_handle(GLuint gl_handle_, uint32_t num_images_, swapchain_usage usage_)
        : type{graphics_api::OPENGL}
        , gl_handle{gl_handle_}
        , num_images{num_images_}
        , usage{usage_} { }

    image_handle(int vk_fd_, int64_t format, size_t alloc_size, uint32_t width_, uint32_t height_, uint32_t num_images_,
                 swapchain_usage usage_)
        : type{graphics_api::VULKAN}
        , vk_handle{vk_fd_, format, alloc_size, width_, height_}
        , num_images{num_images_}
        , usage{usage_} { }
};

// Using arrays as a swapchain
// Array of left eyes, array of right eyes
// This more closely matches the format used by Monado
struct rendered_frame : public switchboard::event {
    std::array<GLuint, 2> swapchain_indices{}; // Does not change between swaps in swapchain
    std::array<GLuint, 2> swap_indices{};      // Which element of the swapchain
    fast_pose_type        render_pose;         // The pose used when rendering this frame.
    time_point            sample_time{};
    time_point            render_time{};

    rendered_frame() = default;

    rendered_frame(std::array<GLuint, 2>&& swapchain_indices_, std::array<GLuint, 2>&& swap_indices_,
                   fast_pose_type render_pose_, time_point sample_time_, time_point render_time_)
        : swapchain_indices{swapchain_indices_}
        , swap_indices{swap_indices_}
        , render_pose(std::move(render_pose_))
        , sample_time(sample_time_)
        , render_time(render_time_) { }
};

struct hologram_input : public switchboard::event {
    uint seq{};

    hologram_input() = default;

    explicit hologram_input(uint seq_)
        : seq{seq_} { }
};

struct signal_to_quad : public switchboard::event {
    ullong seq;

    signal_to_quad(ullong seq_)
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
    duration           offload_duration{};
    unsigned char*     image{};
    time_point         pose_time{};
    Eigen::Vector3f    position;
    Eigen::Quaternionf latest_quaternion;
    Eigen::Quaternionf render_quaternion;

    texture_pose() = default;

    texture_pose(duration offload_duration_, unsigned char* image_, time_point pose_time_, Eigen::Vector3f position_,
                 Eigen::Quaternionf latest_quaternion_, Eigen::Quaternionf render_quaternion_)
        : offload_duration{offload_duration_}
        , image{image_}
        , pose_time{pose_time_}
        , position{std::move(position_)}
        , latest_quaternion{std::move(latest_quaternion_)}
        , render_quaternion{std::move(render_quaternion_)} { }
};
} // namespace ILLIXR
