#include "plugin.hpp"

#include <eigen3/Eigen/Dense>
#include <mutex>
#include <opencv2/opencv.hpp> // Include OpenCV API
#include <optional>
#include <string>
#include <vector>

using namespace ILLIXR;

static constexpr int IMAGE_WIDTH_D4XX  = 640;
static constexpr int IMAGE_HEIGHT_D4XX = 480;
static constexpr int FPS_D4XX          = 30;
static constexpr int GYRO_RATE_D4XX    = 400; // 200 or 400
static constexpr int ACCEL_RATE_D4XX   = 250; // 63 or 250

static constexpr int IMAGE_WIDTH_T26X  = 848;
static constexpr int IMAGE_HEIGHT_T26X = 800;

[[maybe_unused]] realsense::realsense(const std::string& name, phonebook* pb)
    : plugin{name, pb}
    , switchboard_{phonebook_->lookup_impl<switchboard>()}
    , clock_{phonebook_->lookup_impl<relative_clock>()}
    , imu_{switchboard_->get_writer<imu_type>("imu")}
    , cam_{switchboard_->get_writer<cam_type>("cam")}
    , rgb_depth_{switchboard_->get_writer<rgb_depth_type>("rgb_depth")}
    , realsense_cam_{ILLIXR::getenv_or("REALSENSE_CAM", "auto")} {
    spdlogger(std::getenv("REALSENSE_LOG_LEVEL"));
    accel_data_.iteration = -1;
    config_.disable_all_streams();
    configure_camera();
}

void realsense::callback(const rs2::frame& frame) {
    std::lock_guard<std::mutex> lock(mutex_);
    // This lock guarantees that concurrent invocations of `callback` are serialized.
    // Even if the API does not invoke `callback` in parallel, this is still important for the memory-model.
    // Without this lock, prior invocations of `callback` are not necessarily "happens-before" ordered, so accessing
    // persistent variables constitutes a data-race, which is undefined behavior in the C++ memory model.

    // This callback function may start running before the relative clock is started. If that happens, the data
    // timestamps will be messed up. We therefore add this guard to ignore all data samples before the clock is started.
    if (!clock_->is_started()) {
        return;
    }
    if (auto mf = frame.as<rs2::motion_frame>()) {
        std::string s = mf.get_profile().stream_name();

        if (s == "Accel") {
            accel_data_.data      = mf.get_motion_data();
            accel_data_.iteration = iteration_accel_;
            iteration_accel_++;
        }

        if (s == "Gyro") {
            if (last_iteration_accel_ == accel_data_.iteration) {
                return;
            }

            last_iteration_accel_ = accel_data_.iteration;
            rs2_vector accel      = accel_data_.data;
            double     ts         = mf.get_timestamp();
            rs2_vector gyro_data  = mf.get_motion_data();

            // IMU data
            Eigen::Vector3f la = {accel.x, accel.y, accel.z};
            Eigen::Vector3f av = {gyro_data.x, gyro_data.y, gyro_data.z};

            // Time as ullong (nanoseconds)
            auto imu_time = static_cast<ullong>(ts * 1000000);
            if (!first_imu_time_) {
                first_imu_time_      = imu_time;
                first_real_time_imu_ = clock_->now();
            }

            // Time as time_point
            time_point imu_time_point{*first_real_time_imu_ + std::chrono::nanoseconds(imu_time - *first_imu_time_)};

            // Submit to switchboard
            imu_.put(imu_.allocate<imu_type>({imu_time_point, av.cast<double>(), la.cast<double>()}));
        }
    }

    if (auto fs = frame.as<rs2::frameset>()) {
        double ts       = fs.get_timestamp();
        auto   cam_time = static_cast<ullong>(ts * 1000000);
        if (!first_cam_time_) {
            first_cam_time_      = cam_time;
            first_real_time_cam_ = clock_->now();
        }
        time_point cam_time_point{*first_real_time_cam_ + std::chrono::nanoseconds(cam_time - *first_cam_time_)};
        if (cam_select_ == D4XXI) {
            rs2::video_frame ir_frame_left  = fs.get_infrared_frame(1);
            rs2::video_frame ir_frame_right = fs.get_infrared_frame(2);
            rs2::video_frame depth_frame    = fs.get_depth_frame();
            rs2::video_frame rgb_frame      = fs.get_color_frame();
            cv::Mat ir_left = cv::Mat(cv::Size(IMAGE_WIDTH_D4XX, IMAGE_HEIGHT_D4XX), CV_8UC1, (void*) ir_frame_left.get_data());
            cv::Mat ir_right =
                cv::Mat(cv::Size(IMAGE_WIDTH_D4XX, IMAGE_HEIGHT_D4XX), CV_8UC1, (void*) ir_frame_right.get_data());
            cv::Mat rgb   = cv::Mat(cv::Size(IMAGE_WIDTH_D4XX, IMAGE_HEIGHT_D4XX), CV_8UC3, (void*) rgb_frame.get_data());
            cv::Mat depth = cv::Mat(cv::Size(IMAGE_WIDTH_D4XX, IMAGE_HEIGHT_D4XX), CV_16UC1, (void*) depth_frame.get_data());
            cv::Mat converted_depth;
            float   depth_scale = pipeline_.get_active_profile()
                                    .get_device()
                                    .first<rs2::depth_sensor>()
                                    .get_depth_scale(); // for converting measurements into millimeters
            depth.convertTo(converted_depth, CV_32FC1, depth_scale * 1000.f);
            cam_.put(cam_.allocate<cam_type>({cam_time_point, ir_left, ir_right}));
            rgb_depth_.put(rgb_depth_.allocate<rgb_depth_type>({cam_time_point, rgb, depth}));
        } else if (cam_select_ == T26X) {
            rs2::video_frame fisheye_frame_left  = fs.get_fisheye_frame(1);
            rs2::video_frame fisheye_frame_right = fs.get_fisheye_frame(2);
            cv::Mat          fisheye_left =
                cv::Mat(cv::Size(IMAGE_WIDTH_T26X, IMAGE_HEIGHT_T26X), CV_8UC1, (void*) fisheye_frame_left.get_data());
            cv::Mat fisheye_right =
                cv::Mat(cv::Size(IMAGE_WIDTH_T26X, IMAGE_HEIGHT_T26X), CV_8UC1, (void*) fisheye_frame_right.get_data());
            cam_.put(cam_.allocate<cam_type>({cam_time_point, fisheye_left, fisheye_right}));
        }
    }
};

realsense::~realsense() {
    pipeline_.stop();
}

void realsense::find_supported_devices(const rs2::device_list& devices) {
    bool gyro_found{false};
    bool accel_found{false};
    for (rs2::device device : devices) {
        if (device.supports(RS2_CAMERA_INFO_PRODUCT_LINE)) {
            std::string product_line = device.get_info(RS2_CAMERA_INFO_PRODUCT_LINE);
#ifndef NDEBUG
            spdlog::get(name_)->debug("Found Product Line: {}", product_line);
#endif
            if (product_line == "D400") {
#ifndef NDEBUG
                spdlog::get(name_)->debug("Checking for supported streams");
#endif
                std::vector<rs2::sensor> sensors = device.query_sensors();
                for (const rs2::sensor& sensor : sensors) {
                    std::vector<rs2::stream_profile> stream_profiles = sensor.get_stream_profiles();
                    // Currently, all D4XX cameras provide infrared, RGB, and depth, so we only need to check for accel and
                    // gyro
                    for (auto&& sp : stream_profiles) {
                        if (sp.stream_type() == RS2_STREAM_GYRO) {
                            gyro_found = true;
                        }
                        if (sp.stream_type() == RS2_STREAM_ACCEL) {
                            accel_found = true;
                        }
                    }
                }
                if (accel_found && gyro_found) {
                    D4XXI_found_ = true;
#ifndef NDEBUG
                    spdlog::get(name_)->debug("Supported D4XX found!");
#endif
                }
            } else if (product_line == "T200") {
                T26X_found_ = true;
#ifndef NDEBUG
                spdlog::get(name_)->debug("T26X found!");
#endif
            }
        }
    }
    if (!T26X_found_ && !D4XXI_found_) {
#ifndef NDEBUG
        spdlog::get(name_)->warn("No supported Realsense device detected!");
#endif
    }
}

void realsense::configure_camera() {
    rs2::context     ctx;
    rs2::device_list devices = ctx.query_devices();
    // This plugin assumes only one device should be connected to the system. If multiple supported devices are found the
    // preference is to choose D4XX with IMU over T26X systems.
    find_supported_devices(devices);
    if (realsense_cam_ == "auto") {
        if (D4XXI_found_) {
            cam_select_ = D4XXI;
#ifndef NDEBUG
            spdlog::get(name_)->debug("Setting cam_select_: D4XX");
#endif
        } else if (T26X_found_) {
            cam_select_ = T26X;
#ifndef NDEBUG
            spdlog::get(name_)->debug("Setting cam_select_: T26X");
#endif
        }
    } else if ((realsense_cam_ == "D4XX") && D4XXI_found_) {
        cam_select_ = D4XXI;
#ifndef NDEBUG
        spdlog::get(name_)->debug("Setting cam_select_: D4XX");
#endif
    } else if ((realsense_cam_ == "T26X") && T26X_found_) {
        cam_select_ = T26X;
#ifndef NDEBUG
        spdlog::get(name_)->debug("Setting cam_select_: T26X");
#endif
    }
    if (cam_select_ == UNSUPPORTED) {
        ILLIXR::abort("Supported Realsense device NOT found!");
    }
    if (cam_select_ == T26X) {
        // T26X series has fixed options for accel rate, gyro rate, fisheye resolution, and FPS
        config_.enable_stream(RS2_STREAM_ACCEL, RS2_FORMAT_MOTION_XYZ32F); // 62 Hz
        config_.enable_stream(RS2_STREAM_GYRO, RS2_FORMAT_MOTION_XYZ32F);  // 200 Hz
        config_.enable_stream(RS2_STREAM_FISHEYE, 1, RS2_FORMAT_Y8);       // 848x800, 30 FPS
        config_.enable_stream(RS2_STREAM_FISHEYE, 2, RS2_FORMAT_Y8);       // 848x800, 30 FPS
        profiles_ = pipeline_.start(config_, [&](const rs2::frame& frame) {
            this->callback(frame);
        });
    } else if (cam_select_ == D4XXI) {
        config_.enable_stream(RS2_STREAM_ACCEL, RS2_FORMAT_MOTION_XYZ32F,
                              ACCEL_RATE_D4XX); // adjustable to 0, 63 (default), 250 hz
        config_.enable_stream(RS2_STREAM_GYRO, RS2_FORMAT_MOTION_XYZ32F,
                              GYRO_RATE_D4XX); // adjustable set to 0, 200 (default), 400 hz
        config_.enable_stream(RS2_STREAM_INFRARED, 1, IMAGE_WIDTH_D4XX, IMAGE_HEIGHT_D4XX, RS2_FORMAT_Y8, FPS_D4XX);
        config_.enable_stream(RS2_STREAM_INFRARED, 2, IMAGE_WIDTH_D4XX, IMAGE_HEIGHT_D4XX, RS2_FORMAT_Y8, FPS_D4XX);
        config_.enable_stream(RS2_STREAM_COLOR, IMAGE_WIDTH_D4XX, IMAGE_HEIGHT_D4XX, RS2_FORMAT_BGR8, FPS_D4XX);
        config_.enable_stream(RS2_STREAM_DEPTH, IMAGE_WIDTH_D4XX, IMAGE_HEIGHT_D4XX, RS2_FORMAT_Z16, FPS_D4XX);
        profiles_ = pipeline_.start(config_, [&](const rs2::frame& frame) {
            this->callback(frame);
        });
        profiles_.get_device().first<rs2::depth_sensor>().set_option(
            RS2_OPTION_EMITTER_ENABLED, 0.f); // disables IR emitter to use stereo images for SLAM but degrades depth
                                              // quality in low texture environments.
    }
}

PLUGIN_MAIN(realsense)
