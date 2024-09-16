#pragma once

#include "illixr/data_format.hpp"
#include "illixr/error_util.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/plugin.hpp"
#include "illixr/relative_clock.hpp"
#include "illixr/switchboard.hpp"

namespace ILLIXR {
class rk4_integrator : public plugin {
public:
    [[maybe_unused]] rk4_integrator(const std::string& name, phonebook* pb);
    void callback(const switchboard::ptr<const imu_type>& datum);

private:
    void clean_imu_vec(time_point timestamp);
    void propagate_imu_values(time_point real_time);
    static std::vector<imu_type> select_imu_readings(const std::vector<imu_type>& imu_data,
                                                     time_point time_begin,
                                                     time_point time_end);
    static imu_type interpolate_imu(const imu_type& imu1, const imu_type& imu2,
                                                 time_point timestamp);
    static void predict_mean_rk4(const Eigen::Vector4d& quat, const Eigen::Vector3d& pos,
                                 const Eigen::Vector3d& vel, double dt, const Eigen::Vector3d& w_hat1,
                                 const Eigen::Vector3d& a_hat1, const Eigen::Vector3d& w_hat2,
                                 const Eigen::Vector3d& a_hat2, Eigen::Vector4d& new_q,
                                 Eigen::Vector3d& new_v, Eigen::Vector3d& new_p);
    static Eigen::Matrix<double, 4, 4> Omega(Eigen::Matrix<double, 3, 1> w);
    static Eigen::Matrix<double, 4, 1> quatnorm(Eigen::Matrix<double, 4, 1> q_t);
    static Eigen::Matrix<double, 3, 3> skew_x(const Eigen::Matrix<double, 3, 1>& w);
    static Eigen::Matrix<double, 3, 3> quat_2_Rot(const Eigen::Matrix<double, 4, 1>& q);
    static Eigen::Matrix<double, 4, 1> quat_multiply(const Eigen::Matrix<double, 4, 1>& q,
                                                     const Eigen::Matrix<double, 4, 1>& p);

    const std::shared_ptr<switchboard> switchboard_;

    // IMU Data, Sequence Flag, and State Vars Needed
    switchboard::reader<imu_integrator_input> imu_integrator_input_;

    // IMU Biases
    switchboard::writer<imu_raw_type> imu_raw_;
    std::vector<imu_type>             imu_vec_;
    duration                          last_imu_offset_{};
    bool                              has_last_offset_ = false;

    [[maybe_unused]] int    counter_       = 0;
    [[maybe_unused]] int    cam_count_     = 0;
    [[maybe_unused]] int    total_imu_     = 0;
    [[maybe_unused]] double last_cam_time_ = 0;
};

}