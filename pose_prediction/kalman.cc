#include <math.h>
#include <Eigen/Dense>
#include "kalman.hh"

kalman_filter::kalman_filter(std::chrono::time_point<std::chrono::system_clock> init_time) {
    _last_measurement = init_time;
    C << 1, 0, 0, 0,
         0, 0, 1, 0;
}

std::vector<float> kalman_filter::predict_values(imu_sample data) {
    float phi_acc = atan2(data.measurement.ay, sqrt(pow(data.measurement.ax, 2.0) + pow(data.measurement.az, 2.0)));
    float theta_acc = atan2(-data.measurement.ax, sqrt(pow(data.measurement.ay, 2.0) + pow(data.measurement.az, 2.0)));
     
    phi_acc -= _phi_offset;
    theta_acc -= _theta_offset;
        
    data.measurement.gx *= M_PI / 180.0;
    data.measurement.gy *= M_PI / 180.0;
    data.measurement.gz *= M_PI / 180.0;

    float time_interval = std::chrono::duration_cast<std::chrono::milliseconds>
            (data.sample_time - _last_measurement).count();
    _last_measurement = data.sample_time;

    Eigen::MatrixXd A{4,4};
    A << 1, -time_interval, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, -time_interval,
        0, 0, 0, 1;
    
    Eigen::MatrixXd B{4,2};
    B << time_interval, 0,
        0, 0,
        0, time_interval,
        0, 0;

    float phi_dot = data.measurement.gx + sin(_phi_estimate) * tan(_theta_estimate) * data.measurement.gy + 
            cos(_phi_estimate) * tan(_theta_estimate) * data.measurement.gz;
    float theta_dot = cos(_phi_estimate) * data.measurement.gy - sin(_phi_estimate) * data.measurement.gz;

    Eigen::MatrixXd gyro_input{2,1};
    gyro_input << phi_dot,
                theta_dot; 

    _state_estimate = A * _state_estimate + B * gyro_input;
    P = A * (P * A.transpose()) + Q;
        
    Eigen::MatrixXd measurement{2,1};
    measurement << phi_acc,
                theta_acc;

    Eigen::MatrixXd y_tilde = measurement - C * _state_estimate;
    Eigen::MatrixXd S = C * (P * C.transpose()) + R;
    Eigen::MatrixXd K = P * (C.transpose() * (S.inverse()));
    _state_estimate += K * y_tilde;
    P = (Eigen::MatrixXd::Identity(4, 4) - K * C) * P;

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
    return std::vector<float>{_phi_estimate, data.measurement.gy, _theta_estimate};
}
