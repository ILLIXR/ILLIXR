#include <math.h>
#include <Eigen/Dense>
#include "kalman.hh"

kalman_filter::kalman_filter(std::chrono::time_point<std::chrono::system_clock> init_time) {
    _last_measurement = init_time;
    C << 1, 0, 0, 0,
         0, 0, 1, 0;
}

Eigen::Vector3f kalman_filter::predict_values(imu_type data) {
    float phi_acc = atan2(data.linear_a[1], sqrt(pow(data.linear_a[0], 2.0) + pow(data.linear_a[2], 2.0)));
    float theta_acc = atan2(-data.linear_a[0], sqrt(pow(data.linear_a[1], 2.0) + pow(data.linear_a[2], 2.0)));
     
    phi_acc -= _phi_offset;
    theta_acc -= _theta_offset;

    float time_interval = static_cast<float>(std::chrono::duration_cast<std::chrono::nanoseconds>
			    						   (data.time - _last_measurement).count()) / 1000000000.0f;
    _last_measurement = data.time;

    Eigen::MatrixXf A{4,4};
    A << 1, -time_interval, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, -time_interval,
        0, 0, 0, 1;
    
    Eigen::MatrixXf B{4,2};
    B << time_interval, 0,
        0, 0,
        0, time_interval,
        0, 0;

    float phi_dot = data.angular_v[0] + sin(_phi_estimate) * tan(_theta_estimate) * data.angular_v[1] + 
            cos(_phi_estimate) * tan(_theta_estimate) * data.angular_v[2];
    float theta_dot = cos(_phi_estimate) * data.angular_v[1]- sin(_phi_estimate) * data.angular_v[2];

    Eigen::MatrixXf gyro_input{2,1};
    gyro_input << phi_dot,
                theta_dot; 

    _state_estimate = A * _state_estimate + B * gyro_input;
    P = A * (P * A.transpose()) + Q;
        
    Eigen::MatrixXf measurement{2,1};
    measurement << phi_acc,
                theta_acc;

    Eigen::MatrixXf y_tilde = measurement - C * _state_estimate;
    Eigen::MatrixXf S = C * (P * C.transpose()) + R;
    Eigen::MatrixXf K = P * (C.transpose() * (S.inverse()));
    _state_estimate += K * y_tilde;
    P = (Eigen::MatrixXf::Identity(4, 4) - K * C) * P;

    _phi_estimate = _state_estimate(0);
    _theta_estimate = _state_estimate(2);

    /* Reason why we dont predict yaw taken from another project - 
    Set the yaw of actual observation to follow 
	the predicted observation. This is done because 
	the accelerometer will not be able to correct for 
	yaw as change in yaw in the original frame will 
	not be reflected in accelerometer reading. So the 
	idea is to trust the gyro integration of yaw for 
	short term, without any correction. This is done 
	keeping in mind the future requirements of the 
	project. The following step ensures that the gyro 
	integration error does not affect the calculation of 
	innovation, and thus the estimates of roll and pitch. */

    //float phi_degrees = _phi_estimate * (180.0 / M_PI);
    //float theta_degrees = _theta_estimate * (180.0 / M_PI);

    return Eigen::Vector3f(_phi_estimate, data.angular_v[1], _theta_estimate);
}
