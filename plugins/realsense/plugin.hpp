#pragma once

#include "illixr/data_format.hpp"
#include "illixr/opencv_data_types.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/relative_clock.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"

#include <librealsense2/rs.hpp> // Include RealSense Cross Platform API

namespace ILLIXR {
class realsense : public plugin {
public:
    [[maybe_unused]] realsense(const std::string& name, phonebook* pb);
    void callback(const rs2::frame& frame);
    ~realsense() override;

private:
    typedef enum { UNSUPPORTED, D4XXI, T26X } cam_enum;

    typedef struct {
        rs2_vector data;
        int        iteration;
    } accel_type;

    void find_supported_devices(const rs2::device_list& devices);
    void configure_camera();

    const std::shared_ptr<switchboard>          switchboard_;
    const std::shared_ptr<const relative_clock> clock_;
    switchboard::writer<imu_type>               imu_;
    switchboard::writer<cam_type>               cam_;
    switchboard::writer<rgb_depth_type>         rgb_depth_;
    std::mutex                                  mutex_;
    rs2::pipeline_profile                       profiles_;
    rs2::pipeline                               pipeline_;
    rs2::config                                 config_;

    cam_enum cam_select_{UNSUPPORTED};
    bool     D4XXI_found_{false};
    bool     T26X_found_{false};

    accel_type  accel_data_{};
    int         iteration_accel_ = 0;
    int         last_iteration_accel_{};
    std::string realsense_cam_;

    std::optional<ullong>     first_imu_time_;
    std::optional<time_point> first_real_time_imu_;

    std::optional<ullong>     first_cam_time_;
    std::optional<time_point> first_real_time_cam_;
};

}