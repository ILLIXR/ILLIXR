#ifndef KALMAN_HH__
#define KALMAN_HH__

#include <chrono>
#include "common/data_format.hpp"
#include <Eigen/Dense>

using namespace ILLIXR;

class kalman_filter {
    public:
        kalman_filter();
        Eigen::Vector3f predict_values(imu_type, Eigen::Vector3f, float);
        void add_bias(imu_type);
        void init_prediction(pose_type);
        void update_estimates(Eigen::Quaternionf);

    private:
        // We dont do anything with these here but they should be calculated per device which is
        // done by taking the average value of N measurements of the device at rest
        int _bias_count = 0;
        Eigen::Vector3f _linear_accel_bias = {0, 0, 0};
        Eigen::Vector3f _angular_vel_bias = {0, 0, 0};
        time_type _last_bias_measurement = std::chrono::system_clock::now();

        Eigen::MatrixXf P = Eigen::MatrixXf::Identity(3, 3);
        Eigen::MatrixXf Q = Eigen::MatrixXf::Identity(3, 3);
        Eigen::MatrixXf R = Eigen::MatrixXf::Identity(3, 3);
        Eigen::MatrixXf _state_estimate{3, 1};
};

#endif
