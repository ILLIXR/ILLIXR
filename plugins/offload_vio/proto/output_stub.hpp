#pragma once
/**
 * This file exists purely to suppress IDE errors
 */
#error "Missing generated header vio_input.pb.h"
namespace vio_output_proto {
class Vec3 {
public:
    void set_x(double) {}
    void set_y(double) {}
    void set_z(double) {}
};
class Quat {
    public:
    void set_x(double) {}
    void set_y(double) {}
    void set_z(double) {}
    void set_w(double) {}
};

class IMUParams {
public:
    IMUParams() : params_{ILLIXR::time_point(), ILLIXR::clock_duration_{}, {}, {}, {}, {}, {}, {}} {}
    double gyro_noise() const { return params_.params.gyro_noise; }
    double acc_noise() const { return params_.params.acc_noise; }
    double gyro_walk() const { return params_.params.gyro_walk; }
    double acc_walk() const { return params_.params.acc_walk; }
    const Eigen::Matrix<double, 3, 1> n_gravity() const { return params_.params.n_gravity; }
    double imu_integration_sigma() const { return params_.params.imu_integration_sigma; }
    double nominal_rate() const { return params_.params.nominal_rate; }
    ILLIXR::data_format::imu_integrator_input params() const { return params_; }
    void set_gyro_noise(double) {}
    void set_acc_noise(double) {}
    void set_gyro_walk(double) {}
    void set_acc_walk(double) {}
    void set_allocated_n_gravity(Vec3*) {}
    void set_imu_integration_sigma(double) {}
    void set_nominal_rate(double) {}
private:
    ILLIXR::data_format::imu_integrator_input params_;
};
class IMUIntInput {
public:
    IMUIntInput() {}
    int last_cam_integration_time() const { return 0; }
    int t_offset() const { return 0; }
    IMUParams imu_params() const { return params_; }
    const Eigen::Matrix<double, 3, 1> biasacc() const { return params_.params().bias_acc; }
    const Eigen::Matrix<double, 3, 1> biasgyro() const { return params_.params().bias_gyro; }
    const Eigen::Matrix<double, 3, 1> position() const { return params_.params().position; }
    const Eigen::Matrix<double, 3, 1> velocity() const { return params_.params().velocity; }
    const Eigen::Quaterniond rotation() const { return params_.params().quat; }
    void set_t_offset(long) {}
    void set_last_cam_integration_time(long) {}
    void set_allocated_imu_params(IMUParams*) {}
    void set_allocated_biasacc(Vec3*) {}
    void set_allocated_biasgyro(Vec3*) {}
    void set_allocated_position(Vec3*) {}
    void set_allocated_velocity(Vec3*) {}
    void set_allocated_rotation(Quat*) {}
private:
    IMUParams params_;
};
class SlowPose {
public:
    int timestamp() const { return 0; }
    Eigen::Vector3d position() const { return {}; }
    Eigen::Quaternionf rotation() const { return {}; }
    void set_timestamp(long) {}
    void set_allocated_position(Vec3*) {}
    void set_allocated_rotation(Quat*) {}
};
class VIOOutput {
public:
    //void set_real_timestamp(int) {}
    //void set_frame_id(int) {}
    std::string SerializeAsString() {return "";}
    //IMUData* add_imu_data() { return nullptr; }
    void set_allocated_slow_pose(SlowPose*) {}
    void set_allocated_imu_int_input(IMUIntInput*) {}
    bool ParseFromString(std::string) { return false; }
    SlowPose slow_pose() const { return {}; }
    IMUIntInput imu_int_input() const { return {}; }
    void set_end_server_timestamp(unsigned long long) {}
    //double real_timestamp() const { return 0.; }
    //int imu_data_size() const { return 0; }
    //IMUData imu_data(int) const {return {};}
    //CamData cam_data() const { return {}; }
};
}
