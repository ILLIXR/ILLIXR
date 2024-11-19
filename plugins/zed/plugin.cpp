#include <cassert>
#include <chrono>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <utility>


#include "illixr/error_util.hpp"

#include "plugin.hpp"

#define UNITS MILLIMETER

using namespace ILLIXR;

// Set exposure to 8% of camera frame time. This is an empirically determined number
static constexpr unsigned EXPOSURE_TIME_PERCENT = 8;

const record_header __imu_cam_record{"imu_cam",
                                     {
                                         {"iteration_no", typeid(std::size_t)},
                                         {"has_camera", typeid(bool)},
                                     }};

std::shared_ptr<Camera> start_camera(bool get_pose) {
    std::shared_ptr<Camera> zedm = std::make_shared<Camera>();

    assert(zedm != nullptr && "Zed camera should be initialized");

    // Cam setup
    InitParameters init_params;
    init_params.camera_resolution      = (get_pose) ? RESOLUTION::HD720 : RESOLUTION::VGA;
    init_params.coordinate_units       = UNIT::UNITS;                           // For scene reconstruction
    init_params.coordinate_system      = COORDINATE_SYSTEM::RIGHT_HANDED_Z_UP_X_FWD; // Coordinate system used in ROS
    init_params.camera_fps             = (get_pose) ? 60 : 30;                                         // gives best user experience
    init_params.depth_mode             = DEPTH_MODE::PERFORMANCE;
    init_params.depth_stabilization    = true;
    init_params.depth_minimum_distance = 0.3;

    // Open the camera
    ERROR_CODE err = zedm->open(init_params);
    if (err != ERROR_CODE::SUCCESS) {
        spdlog::get("illixr")->info("[zed] {}", toString(err).c_str());
        zedm->close();
    }
    if(get_pose) {
        sl::PositionalTrackingParameters tracking_params;
        err = zedm->enablePositionalTracking(tracking_params);
        if (err != ERROR_CODE::SUCCESS) {
            spdlog::get("illixr")->info("[zed] {}", toString(err).c_str());
            zedm->close();
        }
    }
    zedm->setCameraSettings(VIDEO_SETTINGS::EXPOSURE, EXPOSURE_TIME_PERCENT);

    return zedm;
}

void transform_zed_pose(sl::Transform &from_pose, sl::Transform &to_pose, float ty) {
    sl::Transform transform_;
    transform_.setIdentity();
    transform_.ty = ty;
    to_pose = Transform::inverse(transform_) * from_pose * transform_;
}

zed_camera_thread::zed_camera_thread(const std::string& name_, phonebook* pb_, std::shared_ptr<Camera> zedm_, bool get_pose)
    : threadloop{name_, pb_}
    , switchboard_{pb->lookup_impl<switchboard>()}
    , clock_{pb->lookup_impl<RelativeClock>()}
    , cam_{switchboard_->get_writer<cam_type_zed>("cam_zed")}
    , zed_cam_{std::move(zedm_)}
    , image_size_{zed_cam_->getCameraInformation().camera_configuration.resolution}
    , get_pose_{get_pose}
    , right_eye_offset_{zed_cam_->getCameraInformation().camera_configuration.calibration_parameters.getCameraBaseline()}{
    // runtime_parameters.sensing_mode = SENSING_MODE::STANDARD;
    // Image setup
    imageL_zed_.alloc(image_size_.width, image_size_.height, MAT_TYPE::U8_C4, MEM::CPU);
    imageR_zed_.alloc(image_size_.width, image_size_.height, MAT_TYPE::U8_C4, MEM::CPU);
    rgb_zed_.alloc(image_size_.width, image_size_.height, MAT_TYPE::U8_C4, MEM::CPU);
    depth_zed_.alloc(image_size_.width, image_size_.height, MAT_TYPE::F32_C1, MEM::CPU);
    confidence_zed_.alloc(image_size_.width, image_size_.height, MAT_TYPE::F32_C1, MEM::CPU);

    imageL_ocv_ = slMat2cvMat(imageL_zed_);
    imageR_ocv_ = slMat2cvMat(imageR_zed_);
    rgb_ocv_    = slMat2cvMat(rgb_zed_);
    depth_ocv_  = slMat2cvMat(depth_zed_);
    confidence_ocv_ = slMat2cvMat(confidence_zed_);
}

HandTracking::camera_config zed_camera_thread::get_config() {
    auto cam_conf = zed_cam_->getCameraInformation().camera_configuration;
    sl::CameraParameters left_cam = cam_conf.calibration_parameters.left_cam;
    sl::CameraParameters right_cam = cam_conf.calibration_parameters.right_cam;
    return {{{LEFT_EYE, {left_cam.cx, left_cam.cy, left_cam.v_fov * (M_PI / 180), left_cam.h_fov * (M_PI / 180)}},
             {RIGHT_EYE, {right_cam.cx, right_cam.cy, right_cam.v_fov * (M_PI / 180), right_cam.h_fov * (M_PI / 180)}}},
            cam_conf.resolution.width, cam_conf.resolution.height, cam_conf.fps, cam_conf.calibration_parameters.getCameraBaseline(), ILLIXR::HandTracking::UNITS};
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
    // ullong cam_time = static_cast<ullong>(zedm->getTimestamp(TIME_REFERENCE::IMAGE).getNanoseconds());

    // Retrieve images
    zed_cam_->retrieveImage(imageL_zed_, VIEW::LEFT, MEM::CPU, image_size_);
    zed_cam_->retrieveImage(imageR_zed_, VIEW::RIGHT, MEM::CPU, image_size_);
    zed_cam_->retrieveMeasure(depth_zed_, MEASURE::DEPTH, MEM::CPU, image_size_);
    zed_cam_->retrieveImage(rgb_zed_, VIEW::LEFT, MEM::CPU, image_size_);
    zed_cam_->retrieveMeasure(confidence_zed_, sl::MEASURE::CONFIDENCE);

    multi_pose_map poses;
    if(get_pose_) {
        sl::Pose zed_pose_left;
        //sl::Pose zed_pose_right;
        if (zed_cam_->grab() == ERROR_CODE::SUCCESS) {
            // Get the pose of the camera relative to the world frame
            POSITIONAL_TRACKING_STATE state = zed_cam_->getPosition(zed_pose_left, REFERENCE_FRAME::WORLD);
            if (state == sl::POSITIONAL_TRACKING_STATE::OFF || state == sl::POSITIONAL_TRACKING_STATE::UNAVAILABLE)
                throw std::runtime_error("Tracking failed");
            //zed_pose_right = sl::Pose(zed_pose_left);
            //transform_zed_pose(zed_pose_left.pose_data, zed_pose_right.pose_data, right_eye_offset_);
            pose_type left_eye_pose{time_point(_clock_duration(zed_pose_left.timestamp.getNanoseconds())),
                                    {zed_pose_left.getTranslation().tx, zed_pose_left.getTranslation().ty,
                                     zed_pose_left.getTranslation().tz},
                                    {zed_pose_left.getOrientation().w, zed_pose_left.getOrientation().x,
                                     zed_pose_left.getOrientation().y, zed_pose_left.getOrientation().z}};
            //pose_type right_eye_pose{time_point(_clock_duration(zed_pose_right.timestamp.getNanoseconds())),
            //                         {zed_pose_right.getTranslation().tx, zed_pose_right.getTranslation().ty,
            //                          zed_pose_right.getTranslation().tz},
            //                         {zed_pose_right.getOrientation().w, zed_pose_right.getOrientation().x,
            //                          zed_pose_right.getOrientation().y, zed_pose_right.getOrientation().z}};
            poses = {{LEFT_EYE, left_eye_pose}};
        }
    }
    _clock_duration ts = _clock_duration(zed_cam_->getTimestamp(TIME_REFERENCE::IMAGE).getNanoseconds());
    cam_.put(cam_.allocate<cam_type_zed>({time_point{ts},
                                          imageL_ocv_.clone(), imageR_ocv_.clone(), rgb_ocv_.clone(), depth_ocv_.clone(), confidence_ocv_.clone(), ++serial_no_, poses}));

    RAC_ERRNO_MSG("zed_cam at end of _p_one_iteration");
}


void zed_imu_thread::stop() {
    camera_thread_.stop();
    threadloop::stop();
}

zed_imu_thread::zed_imu_thread(const std::string& name_, phonebook* pb_)
    : threadloop{name_, pb_}
    , switchboard_{pb->lookup_impl<switchboard>()}
    , get_pose_{switchboard_->get_env_bool("ZED_POSE")}
    , zed_cam_{start_camera(get_pose_)}
    , camera_thread_{"zed_camera_thread", pb_, zed_cam_, get_pose_}
    , clock_{pb->lookup_impl<RelativeClock>()}
    , imu_{switchboard_->get_writer<imu_type>("imu")}
    , cam_reader_{switchboard_->get_reader<cam_type_zed>("cam_zed")}
    , cam_publisher_{switchboard_->get_writer<binocular_cam_type>("cam")}
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
        cv::cvtColor(c->at(image::LEFT), left_gray, cv::COLOR_RGB2GRAY);
        cv::cvtColor(c->at(image::RIGHT), right_gray, cv::COLOR_RGB2GRAY);
        cam_publisher_.put(cam_publisher_.allocate<binocular_cam_type>({imu_time_point, cv::Mat{left_gray}, cv::Mat{right_gray}}));
        rgb_depth_.put(rgb_depth_.allocate<rgb_depth_type>({imu_time_point, cv::Mat{c->at(image::RGB)}, cv::Mat{c->at(image::DEPTH)}}));
        last_serial_no_ = c->serial_no;
    }

    last_imu_ts_ = sensors_data_.imu.timestamp;

    RAC_ERRNO_MSG("zed_imu at end of _p_one_iteration");
}

// This line makes the plugin importable by Spindle
PLUGIN_MAIN(zed_imu_thread)
