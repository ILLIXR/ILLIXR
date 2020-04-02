#include <math.h>
#include <Eigen/Dense>
#include "kalman.hh"

kalman_filter::kalman_filter() {
    // Uncertainty in the predicted values
    P.diagonal() << .01, .01, .01;

    // Error in the predicted values
    Q.diagonal() << .01, .01, .01;

    // Uncertainty in measured values (accel)
    R.diagonal() << 100, 100, 100;
}

void kalman_filter::add_bias(imu_type data) {
    if (data.time > _last_bias_measurement) {
        _linear_accel_bias += data.linear_a;
        _angular_vel_bias += data.angular_v;
        _last_bias_measurement = data.time;
        _bias_count += 1;
    }
}

void kalman_filter::init_prediction(pose_type init_pose) {
    _linear_accel_bias /= _bias_count;
    _angular_vel_bias /= _bias_count;
    _linear_accel_bias -= init_pose.orientation * Eigen::Vector3f{0, 0, 9.81};

    std::cout << "Bias Count: " << _bias_count << std::endl;
    std::cout << "Linear Accel Bias: " << _linear_accel_bias(0) << ", " << _linear_accel_bias(1) << ", " << _linear_accel_bias(2) << std::endl;
    std::cout << "Angular Vel Bias: " << _angular_vel_bias(0) << ", " << _angular_vel_bias(1) << ", " << _angular_vel_bias(2) << std::endl << std::endl;

    update_estimates(init_pose.orientation);
}

void kalman_filter::update_estimates(Eigen::Quaternionf fresh_orientation) {
    _state_estimate = fresh_orientation.toRotationMatrix().eulerAngles(0, 1, 2);
}

// We have to pass in an accel rotated to fit world coordinates because roll is bound by -PI and PI
Eigen::Vector3f kalman_filter::predict_values(imu_type data_input, Eigen::Vector3f rotated_accel, float dt) {
    imu_type data = data_input;
    rotated_accel -= _linear_accel_bias;
    data.linear_a -= _linear_accel_bias;
    data.angular_v -= _angular_vel_bias;

	float roll = atan2(-rotated_accel[1], -rotated_accel[2]);
    float pitch = atan2(data.linear_a[0], sqrt(pow(data.linear_a[1], 2) + pow(data.linear_a[2], 2)));
    float yaw = _state_estimate(2);
    
    Eigen::MatrixXf A{3, 3};
    A << 1, 0, 0,
        0, 1, 0,
        0, 0, 1;

    Eigen::MatrixXf B{3, 3};
    B << dt, 0, 0,
        0, dt, 0,
        0, 0, dt;

    _state_estimate = A * _state_estimate + B * data.angular_v;
    P = A * (P * A.transpose()) + Q;

    Eigen::MatrixXf measurement{3,1};
    measurement << roll,
                pitch,
                yaw;

    Eigen::MatrixXf y_tilde = measurement - _state_estimate;
    Eigen::MatrixXf S = P + R;
    Eigen::MatrixXf K = P * (S.inverse());
    _state_estimate += K * y_tilde;

    P = (Eigen::MatrixXf::Identity(3, 3) - K) * P;
    return Eigen::Vector3f(_state_estimate(0), _state_estimate(1), _state_estimate(2));
}
