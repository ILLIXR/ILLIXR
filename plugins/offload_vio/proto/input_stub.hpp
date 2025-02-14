#pragma once
/**
 * This file exists purely to suppress IDE errors
 */
#error "Missing generated header vio_input.pb.h"
namespace vio_input_proto {
class Vec3 {
public:
    void set_x(double) {}
    void set_y(double) {}
    void set_z(double) {}
};
class IMUData {
public:
    void set_timestamp(int) {}
    void set_allocated_angular_vel(Vec3*) {}
    void set_allocated_linear_accel(Vec3*) {}
    int timestamp() const { return 0; }
    Eigen::Vector3d angular_vel() const { return {}; }
    Eigen::Vector3d linear_accel() const { return {}; }
};
class CamData {
public:
    void set_timestamp(int) {}
    void set_rows(int) {}
    void set_cols(int) {}
    void set_img0_data(void*, double) {}
    void set_img1_data(void*, double) {}
    std::string img0_data() const { return {}; }
    std::string img1_data() const { return {}; }
    int rows() const { return 0; }
    int cols() const { return 0; }
    int timestamp() const { return 0; }
};
class IMUCamVec {
public:
    void set_real_timestamp(int) {}
    void set_frame_id(int) {}
    std::string SerializeAsString() {return "";}
    IMUData* add_imu_data() { return nullptr; }
    void set_allocated_cam_data(CamData*) {}
    bool ParseFromString(std::string) { return false; }
    double real_timestamp() const { return 0.; }
    int imu_data_size() const { return 0; }
    IMUData imu_data(int) const {return {};}
    CamData cam_data() const { return {}; }
};
}
