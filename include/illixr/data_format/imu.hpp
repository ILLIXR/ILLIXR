#pragma once

#include "illixr/switchboard.hpp"

#include <eigen3/Eigen/Dense>

namespace ILLIXR::data_format {
/**
 * Inertial Measurement Unit representation
 */
struct [[maybe_unused]] imu_type : switchboard::event {
    time_point      time;
    Eigen::Vector3d angular_v;
    Eigen::Vector3d linear_a;

    imu_type(time_point time_, Eigen::Vector3d angular_v_, Eigen::Vector3d linear_a_)
        : time{time_}
        , angular_v{std::move(angular_v_)}
        , linear_a{std::move(linear_a_)} { }
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

/**
 * IMU biases, initialization params, and slow pose needed by the IMU integrator
 */
struct [[maybe_unused]] imu_integrator_input : public switchboard::event {
    time_point last_cam_integration_time;
    duration   t_offset;
    imu_params params;

    Eigen::Vector3d             bias_acc;
    Eigen::Vector3d             bias_gyro;
    Eigen::Matrix<double, 3, 1> position;
    Eigen::Matrix<double, 3, 1> velocity;
    Eigen::Quaterniond          quat;

    imu_integrator_input(time_point last_cam_integration_time_, duration t_offset_, imu_params params_,
                         Eigen::Vector3d biasAcc_, Eigen::Vector3d biasGyro_, Eigen::Matrix<double, 3, 1> position_,
                         Eigen::Matrix<double, 3, 1> velocity_, Eigen::Quaterniond quat_)
        : last_cam_integration_time{last_cam_integration_time_}
        , t_offset{t_offset_}
        , params{std::move(params_)}
        , bias_acc{std::move(biasAcc_)}
        , bias_gyro{std::move(biasGyro_)}
        , position{std::move(position_)}
        , velocity{std::move(velocity_)}
        , quat{std::move(quat_)} { }
};

/**
 * Output of the IMU integrator to be used by pose prediction
 */
struct [[maybe_unused]] imu_raw_type : public switchboard::event {
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
    time_point                  cam_time;

    imu_raw_type(Eigen::Matrix<double, 3, 1> w_hat_, Eigen::Matrix<double, 3, 1> a_hat_, Eigen::Matrix<double, 3, 1> w_hat2_,
                 Eigen::Matrix<double, 3, 1> a_hat2_, Eigen::Matrix<double, 3, 1> pos_, Eigen::Matrix<double, 3, 1> vel_,
                 Eigen::Quaterniond quat_, time_point imu_time_, time_point cam_time_)
        : w_hat{std::move(w_hat_)}
        , a_hat{std::move(a_hat_)}
        , w_hat2{std::move(w_hat2_)}
        , a_hat2{std::move(a_hat2_)}
        , pos{std::move(pos_)}
        , vel{std::move(vel_)}
        , quat{std::move(quat_)}
        , imu_time{imu_time_} 
        , cam_time{cam_time_}
    { }
};

} // namespace ILLIXR::data_format
