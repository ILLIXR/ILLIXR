#pragma once

#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"
#include "illixr/zed_cam.hpp"
#include "zed_camera.hpp"
#include "include/zed_opencv.hpp"

namespace ILLIXR {
class zed_camera_thread : public threadloop {
public:
    zed_camera_thread(const std::string& name, phonebook* pb, std::shared_ptr<zed_camera> zed_cam);

protected:
    skip_option _p_should_skip() override;
    void        _p_one_iteration() override;

private:
    const std::shared_ptr<switchboard>         switchboard_;
    const std::shared_ptr<const RelativeClock> clock_;
    switchboard::writer<cam_type_zed>          cam_;
    std::shared_ptr<zed_camera>                zed_cam_;
    sl::Resolution                             image_size_;
    sl::RuntimeParameters                      runtime_parameters_;
    std::size_t                                serial_no_{0};

    sl::Mat imageL_zed_;
    sl::Mat imageR_zed_;
    sl::Mat depth_zed_;
    sl::Mat rgb_zed_;
    sl::Mat confidence_zed_;

    cv::Mat               imageL_ocv_;
    cv::Mat               imageR_ocv_;
    cv::Mat               depth_ocv_;
    cv::Mat               rgb_ocv_;
    cv::Mat               confidence_ocv_;
    std::optional<ullong> first_imu_time_;
};

}