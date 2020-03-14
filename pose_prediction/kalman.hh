#ifndef KALMAN_HH__
#define KALMAN_HH__

#include <chrono>
#include "pose_prediction.hh"

using namespace ILLIXR;

class kalman_filter {
    public:
        kalman_filter(std::chrono::time_point<std::chrono::system_clock> _init_time) {}
        std::vector<float> predict_values(imu_sample);

    private:
        // We dont do anything with these here but they should be calculated per device which is
        // done by taking the average value of N measurements of the device at rest
        float _phi_offset = 0;
        float _theta_offset = 0;

        float _phi_estimate = 0;
        float _theta_estimate = 0;

        Eigen::MatrixXd C;
        Eigen::MatrixXd P = Eigen::MatrixXd::Identity(4, 4);
        Eigen::MatrixXd Q = Eigen::MatrixXd::Identity(4, 4);
        Eigen::MatrixXd R = Eigen::MatrixXd::Identity(2, 2);
        Eigen::MatrixXd _state_estimate;
        
        std::chrono::time_point<std::chrono::system_clock> _last_measurement;
};

#endif