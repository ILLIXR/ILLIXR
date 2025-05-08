#pragma once

#include "illixr/data_format/imu.hpp"
#include "illixr/data_format/opencv_data_types.hpp"
#include "illixr/data_format/pose.hpp"
#include "illixr/threadloop.hpp"
#include "zed_camera_thread.hpp"

namespace ILLIXR {

class zed_imu_thread : public threadloop {
public:
    [[maybe_unused]] zed_imu_thread(const std::string& name, phonebook* pb);
    void stop() override;
    void start() override;
    ~zed_imu_thread() override;

protected:
    skip_option _p_should_skip() override;
    void        _p_one_iteration() override;

private:
    std::shared_ptr<zed_camera> start_camera();

    const std::shared_ptr<switchboard> switchboard_;
    std::shared_ptr<zed_camera>        zed_cam_;
    zed_camera_thread                  camera_thread_;

    const std::shared_ptr<const relative_clock>          clock_;
    switchboard::writer<data_format::imu_type>           imu_;
    switchboard::reader<data_format::cam_type_zed>       cam_reader_;
    switchboard::writer<data_format::binocular_cam_type> cam_publisher_;
    switchboard::writer<data_format::rgb_depth_type>     rgb_depth_;
    switchboard::writer<data_format::camera_data>        cam_conf_pub_;

    // IMU
    sl::SensorsData sensors_data_;
    sl::Timestamp   last_imu_ts_    = 0;
    std::size_t     last_serial_no_ = 0;

    // Logger
    record_coalescer it_log_;

    std::optional<ullong>     first_imu_time_;
    std::optional<time_point> first_real_time_;
};

} // namespace ILLIXR
