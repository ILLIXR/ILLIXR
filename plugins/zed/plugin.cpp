#include "plugin.hpp"

#include "illixr/error_util.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <memory>
#include <opencv2/opencv.hpp>
#include <optional>
#include <string>

using namespace ILLIXR;
using namespace ILLIXR::data_format;

constexpr int EXPOSURE_TIME_PERCENT = 30;

const record_header __imu_cam_record{"imu_cam",
                                     {
                                         {"iteration_no", typeid(std::size_t)},
                                         {"has_camera", typeid(bool)},
                                     }};

std::shared_ptr<zed_camera> zed_imu_thread::start_camera() {
    std::shared_ptr<zed_camera> zed_cam            = std::make_shared<zed_camera>(switchboard_);
    bool                        with_hand_tracking = false;
    assert(zed_cam != nullptr && "Zed camera should be initialized");

    // Cam setup
    sl::InitParameters init_params;
    init_params.camera_resolution      = (with_hand_tracking) ? sl::RESOLUTION::HD720 : sl::RESOLUTION::VGA;
    // init_params.coordinate_units       = sl::UNIT::UNITS;                          // For scene reconstruction
    init_params.coordinate_units       = sl::UNIT::MILLIMETER;                      // For IMU
    // init_params.coordinate_system      = sl::COORDINATE_SYSTEM::RIGHT_HANDED_Y_UP; // Coordinate system used in ROS
    init_params.coordinate_system      = sl::COORDINATE_SYSTEM::RIGHT_HANDED_Z_UP_X_FWD;
    init_params.camera_fps             = 30;                                       // gives the best user experience
    init_params.depth_mode             = (with_hand_tracking) ? sl::DEPTH_MODE::QUALITY : sl::DEPTH_MODE::PERFORMANCE;
    init_params.depth_stabilization    = true;
    init_params.depth_minimum_distance = 0.3;

    // Open the camera
    sl::ERROR_CODE err = zed_cam->open(init_params);
    if (err != sl::ERROR_CODE::SUCCESS) {
        spdlog::get("illixr")->error("[zed] {}", toString(err).c_str());
        throw std::runtime_error("ZED camera could not be initialized");
    }
    zed_cam->setCameraSettings(sl::VIDEO_SETTINGS::EXPOSURE, EXPOSURE_TIME_PERCENT);
    return zed_cam;
}

void zed_imu_thread::stop() {
    camera_thread_.stop();
    threadloop::stop();
}

zed_imu_thread::zed_imu_thread(const std::string& name_, phonebook* pb_)
    : threadloop{name_, pb_}
    , switchboard_{phonebook_->lookup_impl<switchboard>()}
    , zed_cam_{start_camera()}
    , camera_thread_{"zed_camera_thread", pb_, zed_cam_}
    , clock_{phonebook_->lookup_impl<relative_clock>()}
    , imu_{switchboard_->get_writer<imu_type>("imu")}
    , cam_reader_{switchboard_->get_reader<cam_type_zed>("cam_zed")}
    , cam_publisher_{switchboard_->get_writer<binocular_cam_type>("cam")}
    , rgb_depth_{switchboard_->get_writer<rgb_depth_type>("rgb_depth")}
    , cam_conf_pub_{switchboard_->get_writer<camera_data>("cam_data")}
    , it_log_{record_logger_} { }

// destructor
zed_imu_thread::~zed_imu_thread() {
    zed_cam_->close();
}

void zed_imu_thread::start() {
    camera_thread_.start();
    threadloop::start();
    cam_conf_pub_.put(cam_conf_pub_.allocate<camera_data>(camera_data{zed_cam_->get_config()}));
}

threadloop::skip_option zed_imu_thread::_p_should_skip() {
    zed_cam_->getSensorsData(sensors_data_, sl::TIME_REFERENCE::CURRENT);
    if (sensors_data_.imu.timestamp > last_imu_ts_) {
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
        return skip_option::run;
    } else {
        return skip_option::skip_and_yield;
    }
}

void zed_imu_thread::_p_one_iteration() {
    RAC_ERRNO_MSG("zed at start of _p_one_iteration");
    // std::cout << "IMU Rate: " << sensors_data.imu.effective_rate << "\n" << std::endl;

    // Time as ullong (nanoseconds)
    auto imu_time = static_cast<ullong>(sensors_data_.imu.timestamp.getNanoseconds());

    // Time as time_point
    if (!first_imu_time_) {
        first_imu_time_  = imu_time;
        first_real_time_ = clock_->now();
    }
    // _m_first_real_time is the time point when the system receives the first IMU sample
    // Timestamp for later IMU samples is its dataset time difference from the first sample added to _m_first_real_time
    time_point imu_time_point{*first_real_time_ + std::chrono::nanoseconds(imu_time - *first_imu_time_)};

    // Linear Acceleration and Angular Velocity (av converted from deg/s to rad/s)
    Eigen::Vector3f la = {sensors_data_.imu.linear_acceleration_uncalibrated.x,
                          sensors_data_.imu.linear_acceleration_uncalibrated.y,
                          sensors_data_.imu.linear_acceleration_uncalibrated.z};
    Eigen::Vector3f av = {static_cast<float>(sensors_data_.imu.angular_velocity_uncalibrated.x * (M_PI / 180)),
                          static_cast<float>(sensors_data_.imu.angular_velocity_uncalibrated.y * (M_PI / 180)),
                          static_cast<float>(sensors_data_.imu.angular_velocity_uncalibrated.z * (M_PI / 180))};

    imu_.put(imu_.allocate<imu_type>({imu_time_point, av.cast<double>(), la.cast<double>()}));

    switchboard::ptr<const cam_type_zed> c = cam_reader_.get_ro_nullable();
    if (c && c->serial_no != last_serial_no_) {
        cv::Mat left_gray, right_gray;
        cv::cvtColor(c->at(image::LEFT_EYE), left_gray, cv::COLOR_RGB2GRAY);
        cv::cvtColor(c->at(image::RIGHT_EYE), right_gray, cv::COLOR_RGB2GRAY);
        cam_publisher_.put(
            cam_publisher_.allocate<binocular_cam_type>({imu_time_point, cv::Mat{left_gray}, cv::Mat{right_gray}}));
        rgb_depth_.put(
            rgb_depth_.allocate<rgb_depth_type>({imu_time_point, cv::Mat{c->at(image::RGB)}, cv::Mat{c->at(image::DEPTH)}}));
        last_serial_no_ = c->serial_no;
    }

    last_imu_ts_.setNanoseconds(sensors_data_.imu.timestamp.getNanoseconds());

    RAC_ERRNO_MSG("zed_imu at end of _p_one_iteration");
}

// This line makes the plugin importable by Spindle
PLUGIN_MAIN(zed_imu_thread)
