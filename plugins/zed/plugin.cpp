#include "plugin.hpp"

#include "illixr/error_util.hpp"

#include <cassert>
#include <chrono>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <utility>

using namespace ILLIXR;

// Set exposure to 8% of camera frame time. This is an empirically determined number
static constexpr unsigned EXPOSURE_TIME_PERCENT = 8;

// const record_header __imu_cam_record{"imu_cam",
//                                      {
//                                          {"iteration_no", typeid(std::size_t)},
//                                          {"has_camera", typeid(bool)},
//                                      }};

std::shared_ptr<Camera> start_camera() {
    std::shared_ptr<Camera> zedm = std::make_shared<Camera>();

    assert(zedm != nullptr && "Zed camera should be initialized");

    // Cam setup
    InitParameters init_params;
    init_params.camera_resolution      = RESOLUTION::VGA;
    init_params.coordinate_units       = UNIT::MILLIMETER;                           // For scene reconstruction
    init_params.coordinate_system      = COORDINATE_SYSTEM::RIGHT_HANDED_Z_UP_X_FWD; // Coordinate system used in ROS
    init_params.camera_fps             = 30;                                         // gives best user experience
    init_params.depth_mode             = DEPTH_MODE::PERFORMANCE;
    init_params.depth_stabilization    = true;
    init_params.depth_minimum_distance = 0.3;

    // Open the camera
    ERROR_CODE err = zedm->open(init_params);
    if (err != ERROR_CODE::SUCCESS) {
        spdlog::get("illixr")->info("[zed] {}", toString(err).c_str());
        zedm->close();
    }

    zedm->setCameraSettings(VIDEO_SETTINGS::EXPOSURE, EXPOSURE_TIME_PERCENT);

    return zedm;
}

zed_camera_thread::zed_camera_thread(const std::string& name, phonebook* pb, std::shared_ptr<Camera> zedm_)
    : threadloop{name, pb}
    , switchboard_{phonebook_->lookup_impl<switchboard>()}
    , clock_{phonebook_->lookup_impl<relative_clock>()}
    , cam_{switchboard_->get_writer<cam_type_zed>("cam_zed")}
    , zed_cam_{std::move(zedm_)}
    , image_size_{zed_cam_->getCameraInformation().camera_configuration.resolution} {
    // runtime_parameters_.sensing_mode = SENSING_MODE::STANDARD;
    // Image setup
    imageL_zed_.alloc(image_size_.width, image_size_.height, MAT_TYPE::U8_C1, MEM::CPU);
    imageR_zed_.alloc(image_size_.width, image_size_.height, MAT_TYPE::U8_C1, MEM::CPU);
    rgb_zed_.alloc(image_size_.width, image_size_.height, MAT_TYPE::U8_C4, MEM::CPU);
    depth_zed_.alloc(image_size_.width, image_size_.height, MAT_TYPE::F32_C1, MEM::CPU);

    imageL_ocv_ = slMat_to_cvMat(imageL_zed_);
    imageR_ocv_ = slMat_to_cvMat(imageR_zed_);
    rgb_ocv_    = slMat_to_cvMat(rgb_zed_);
    depth_ocv_  = slMat_to_cvMat(depth_zed_);
}

threadloop::skip_option zed_camera_thread::_p_should_skip() {
    if (zed_cam_->grab(runtime_parameters_) == ERROR_CODE::SUCCESS) {
        return skip_option::run;
    } else {
        return skip_option::skip_and_spin;
    }
}

void zed_camera_thread::_p_one_iteration() {
    RAC_ERRNO_MSG("zed at start of _p_one_iteration");

    // Time as ullong (nanoseconds)
    // ullong cam_time = static_cast<ullong>(zed_cam_->getTimestamp(TIME_REFERENCE::IMAGE).getNanoseconds());

    // Retrieve images
    zed_cam_->retrieveImage(imageL_zed_, VIEW::LEFT_GRAY, MEM::CPU, image_size_);
    zed_cam_->retrieveImage(imageR_zed_, VIEW::RIGHT_GRAY, MEM::CPU, image_size_);
    zed_cam_->retrieveMeasure(depth_zed_, MEASURE::DEPTH, MEM::CPU, image_size_);
    zed_cam_->retrieveImage(rgb_zed_, VIEW::LEFT, MEM::CPU, image_size_);

    cam_.put(cam_.allocate<cam_type_zed>({cv::Mat{imageL_ocv_.clone()}, cv::Mat{imageR_ocv_.clone()}, cv::Mat{rgb_ocv_.clone()},
                                          cv::Mat{depth_ocv_.clone()}, ++serial_no_}));

    RAC_ERRNO_MSG("zed_cam at end of _p_one_iteration");
}

void zed_imu_thread::stop() {
    camera_thread_.stop();
    threadloop::stop();
}

[[maybe_unused]] zed_imu_thread::zed_imu_thread(const std::string& name, phonebook* pb)
    : threadloop{name, pb}
    , zed_cam_{start_camera()}
    , camera_thread_{"zed_camera_thread", pb, zed_cam_}
    , switchboard_{phonebook_->lookup_impl<switchboard>()}
    , clock_{phonebook_->lookup_impl<relative_clock>()}
    , imu_{switchboard_->get_writer<imu_type>("imu")}
    , cam_reader_{switchboard_->get_reader<cam_type_zed>("cam_zed")}
    , cam_publisher_{switchboard_->get_writer<cam_type>("cam")}
    , rgb_depth_{switchboard_->get_writer<rgb_depth_type>("rgb_depth")}
    , it_log_{record_logger_} {
    camera_thread_.start();
}

// destructor
zed_imu_thread::~zed_imu_thread() {
    zed_cam_->close();
}

threadloop::skip_option zed_imu_thread::_p_should_skip() {
    zed_cam_->getSensorsData(sensors_data_, TIME_REFERENCE::CURRENT);
    if (sensors_data_.imu.timestamp > last_imu_ts_) {
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
        return skip_option::run;
    } else {
        return skip_option::skip_and_yield;
    }
}

void zed_imu_thread::_p_one_iteration() {
    RAC_ERRNO_MSG("zed at start of _p_one_iteration");

    // std::cout << "IMU Rate: " << sensors_data_.imu.effective_rate << "\n" << std::endl;

    // Time as ullong (nanoseconds)
    auto imu_time = static_cast<ullong>(sensors_data_.imu.timestamp.getNanoseconds());

    // Time as time_point
    if (!first_imu_time_) {
        first_imu_time_  = imu_time;
        first_real_time_ = clock_->now();
    }
    // first_real_time_ is the time point when the system receives the first IMU sample
    // Timestamp for later IMU samples is its dataset time difference from the first sample added to first_real_time_
    time_point imu_time_point{*first_real_time_ + std::chrono::nanoseconds(imu_time - *first_imu_time_)};

    // Linear Acceleration and Angular Velocity (av converted from deg/s to rad/s)
    Eigen::Vector3f la = {sensors_data_.imu.linear_acceleration_uncalibrated.x,
                          sensors_data_.imu.linear_acceleration_uncalibrated.y,
                          sensors_data_.imu.linear_acceleration_uncalibrated.z};
    Eigen::Vector3f av = {static_cast<float>(sensors_data_.imu.angular_velocity_uncalibrated.x * (M_PI / 180)),
                          static_cast<float>(sensors_data_.imu.angular_velocity_uncalibrated.y * (M_PI / 180)),
                          static_cast<float>(sensors_data_.imu.angular_velocity_uncalibrated.z * (M_PI / 180))};

    imu_.put(imu_.allocate<imu_type>({imu_time_point, av.cast<double>(), la.cast<double>()}));

    switchboard::ptr<const cam_type_zed> cam_type_z = cam_reader_.get_ro_nullable();
    if (cam_type_z && cam_type_z->serial_no != last_serial_no_) {
        cam_publisher_.put(
            cam_publisher_.allocate<cam_type>({imu_time_point, cv::Mat{cam_type_z->img0}, cv::Mat{cam_type_z->img1}}));
        rgb_depth_.put(
            rgb_depth_.allocate<rgb_depth_type>({imu_time_point, cv::Mat{cam_type_z->rgb}, cv::Mat{cam_type_z->depth}}));
        last_serial_no_ = cam_type_z->serial_no;
    }

    last_imu_ts_ = sensors_data_.imu.timestamp;

    RAC_ERRNO_MSG("zed_imu at end of _p_one_iteration");
}

// This line makes the plugin importable by Spindle
PLUGIN_MAIN(zed_imu_thread)
