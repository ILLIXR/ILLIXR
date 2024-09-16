#pragma once

#include "illixr/data_format.hpp"
#include "illixr/opencv_data_types.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"

#include "zed_opencv.hpp"


namespace ILLIXR {
struct cam_type_zed : public switchboard::event {
    cam_type_zed(cv::Mat _img0, cv::Mat _img1, cv::Mat _rgb, cv::Mat _depth, std::size_t _serial_no)
        : img0{std::move(_img0)}
        , img1{std::move(_img1)}
        , rgb{std::move(_rgb)}
        , depth{std::move(_depth)}
        , serial_no{_serial_no} { }

    cv::Mat     img0;
    cv::Mat     img1;
    cv::Mat     rgb;
    cv::Mat     depth;
    std::size_t serial_no;
};

class zed_camera_thread : public threadloop {
public:
    zed_camera_thread(const std::string& name, phonebook* pb, std::shared_ptr<Camera> zedm_);

protected:
    skip_option _p_should_skip() override;
    void _p_one_iteration() override;
private:
    const std::shared_ptr<switchboard>          switchboard_;
    const std::shared_ptr<const relative_clock> clock_;
    switchboard::writer<cam_type_zed>           cam_;
    std::shared_ptr<Camera>                     zed_cam_;
    Resolution                                  image_size_;
    RuntimeParameters                           runtime_parameters_;
    std::size_t                                 serial_no_{0};

    Mat imageL_zed_;
    Mat imageR_zed_;
    Mat depth_zed_;
    Mat rgb_zed_;

    cv::Mat imageL_ocv_;
    cv::Mat imageR_ocv_;
    cv::Mat depth_ocv_;
    cv::Mat rgb_ocv_;

    std::optional<ullong> first_imu_time_;
};

class zed_imu_thread : public threadloop {
public:
    [[maybe_unused]] zed_imu_thread(const std::string& name, phonebook* pb);
    void stop() override;
    ~zed_imu_thread() override;

protected:
    skip_option _p_should_skip() override;
    void _p_one_iteration() override;

private:
    std::shared_ptr<Camera> zed_cam_;
    zed_camera_thread       camera_thread_;

    const std::shared_ptr<switchboard>          switchboard_;
    const std::shared_ptr<const relative_clock> clock_;
    switchboard::writer<imu_type>               imu_;
    switchboard::reader<cam_type_zed>           cam_reader_;
    switchboard::writer<cam_type>               cam_publisher_;
    switchboard::writer<rgb_depth_type>         rgb_depth_;

    // IMU
    SensorsData sensors_data_;
    Timestamp   last_imu_ts_    = 0;
    std::size_t last_serial_no_ = 0;

    // Logger
    record_coalescer it_log_;

    std::optional<ullong>     first_imu_time_;
    std::optional<time_point> first_real_time_;
};

}