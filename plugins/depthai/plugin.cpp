// ILLIXR includes
#include "plugin.hpp"

#include <chrono>
#include <eigen3/Eigen/Core>
#include <functional>
#include <mutex>
#include <opencv2/opencv.hpp>
#include <string>

using namespace ILLIXR;
using namespace ILLIXR::data_format;

[[maybe_unused]] depthai::depthai(const std::string& name, phonebook* pb)
    : plugin{name, pb}
    , switchboard_{phonebook_->lookup_impl<switchboard>()}
    , clock_{phonebook_->lookup_impl<relative_clock>()}
    , imu_writer_{switchboard_->get_writer<imu_type>("imu")}
    , cam_writer_{switchboard_->get_writer<binocular_cam_type>("cam")}
    , rgb_depth_{switchboard_->get_writer<rgb_depth_type>("rgb_depth")} // Initialize DepthAI pipeline and device
    , device_{create_camera_pipeline()} {
    spdlogger(std::getenv("DEPTHAI_LOG_LEVEL"));
#ifndef NDEBUG
    spdlog::get(name)->debug("pipeline started");
#endif
    color_queue_                           = device_.getOutputQueue("preview", 1, false);
    depth_queue_                           = device_.getOutputQueue("depth", 1, false);
    rectif_left_queue_                     = device_.getOutputQueue("rectified_left", 1, false);
    rectif_right_queue_                    = device_.getOutputQueue("rectified_right", 1, false);
    imu_queue_                             = device_.getOutputQueue("imu", 1, false);
    std::function<void(void)> imu_callback = [&]() {
        callback();
    };
    imu_queue_->addCallback(imu_callback);
    test_time_point_ = std::chrono::steady_clock::now();
}

void depthai::callback() {
    std::lock_guard<std::mutex> lock(mutex_);
    // Check for available data
    bool color_go      = color_queue_->has<dai::ImgFrame>();
    bool depth_go      = depth_queue_->has<dai::ImgFrame>();
    bool rectifL_go    = rectif_left_queue_->has<dai::ImgFrame>();
    bool rectifR_go    = rectif_right_queue_->has<dai::ImgFrame>();
    bool imu_packet_go = imu_queue_->has<dai::IMUData>();

#ifndef NDEBUG
    if (color_go) {
        rgb_count_++;
    }
    if (depth_go) {
        depth_count_++;
    }
    if (rectifL_go) {
        left_count_++;
    }
    if (rectifR_go) {
        right_count_++;
    }
#endif

    if (rectifR_go && rectifL_go && depth_go && color_go) {
#ifndef NDEBUG
        all_count_++;
#endif
        auto color_frame = color_queue_->tryGet<dai::ImgFrame>();
        auto depth_frame = depth_queue_->tryGet<dai::ImgFrame>();
        auto rectifL     = rectif_left_queue_->tryGet<dai::ImgFrame>();
        auto rectifR     = rectif_right_queue_->tryGet<dai::ImgFrame>();

        ullong cam_time = static_cast<ullong>(
            std::chrono::time_point_cast<std::chrono::nanoseconds>(color_frame->getTimestamp()).time_since_epoch().count());
        if (!first_cam_time_) {
            first_cam_time_      = cam_time;
            first_real_cam_time_ = clock_->now();
        }

        time_point cam_time_point{*first_real_cam_time_ + std::chrono::nanoseconds(cam_time - *first_cam_time_)};

        cv::Mat color = cv::Mat(static_cast<int>(color_frame->getHeight()), static_cast<int>(color_frame->getWidth()), CV_8UC3,
                                color_frame->getData().data());
        cv::Mat rgb_out{color.clone()};
        cv::Mat rectified_left_frame = cv::Mat(static_cast<int>(rectifL->getHeight()), static_cast<int>(rectifL->getWidth()),
                                               CV_8UC1, rectifL->getData().data());
        cv::Mat left_out{rectified_left_frame.clone()};
        cv::flip(left_out, left_out, 1);
        cv::Mat rectified_right_frame = cv::Mat(static_cast<int>(rectifR->getHeight()), static_cast<int>(rectifR->getWidth()),
                                                CV_8UC1, rectifR->getData().data());
        cv::Mat right_out{rectified_right_frame.clone()};
        cv::flip(right_out, right_out, 1);

        cv::Mat depth = cv::Mat(static_cast<int>(depth_frame->getHeight()), static_cast<int>(depth_frame->getWidth()), CV_16UC1,
                                depth_frame->getData().data());
        cv::Mat converted_depth;
        depth.convertTo(converted_depth, CV_32FC1, 1000.f);

        cam_writer_.put(cam_writer_.allocate<binocular_cam_type>({cam_time_point, cv::Mat{left_out}, cv::Mat{right_out}}));
        rgb_depth_.put(rgb_depth_.allocate<rgb_depth_type>({cam_time_point, cv::Mat{rgb_out}, cv::Mat{converted_depth}}));
    }

    std::chrono::time_point<std::chrono::steady_clock, std::chrono::steady_clock::duration> gyro_ts;
    Eigen::Vector3d                                                                         la;
    Eigen::Vector3d                                                                         av;

    if (imu_packet_go) {
        auto imu_packet = imu_queue_->tryGet<dai::IMUData>();
#ifndef NDEBUG
        if (imu_packet_ == 0) {
            first_packet_time_ = std::chrono::steady_clock::now();
        }
        imu_packet_++;
#endif

        auto imu_data = imu_packet->packets;
        for (auto& imu_datum : imu_data) {
            gyro_ts = std::chrono::time_point_cast<std::chrono::nanoseconds>(imu_datum.gyroscope.timestamp.get());
            if (gyro_ts <= test_time_point_) {
                return;
            }
            test_time_point_ = gyro_ts;
            la               = {imu_datum.acceleroMeter.x, imu_datum.acceleroMeter.y, imu_datum.acceleroMeter.z};
            av               = {imu_datum.gyroscope.x, imu_datum.gyroscope.y, imu_datum.gyroscope.z};
        }

        // Time as ullong (nanoseconds)
        ullong imu_time = static_cast<ullong>(gyro_ts.time_since_epoch().count());
        if (!first_imu_time_) {
            first_imu_time_      = imu_time;
            first_real_imu_time_ = clock_->now();
        }

        time_point imu_time_point{*first_real_imu_time_ + std::chrono::nanoseconds(imu_time - *first_imu_time_)};

// Submit to switchboard
#ifndef NDEBUG
        imu_pub_++;
#endif
        imu_writer_.put(imu_writer_.allocate<imu_type>({
            imu_time_point,
            av,
            la,
        }));
    }
}

depthai::~depthai() {
#ifndef NDEBUG
    spdlog::get(name_)->debug("Destructor: Packets Received {} Published: IMU: {} RGB-D: {}", imu_packet_, imu_pub_, rgbd_pub_);
    auto dur = std::chrono::steady_clock::now() - first_packet_time_;
    spdlog::get(name_)->debug("Time since first packet: {} ms",
                              std::chrono::duration_cast<std::chrono::milliseconds>(dur).count());
    spdlog::get(name_)->debug("RGB: {} Left: {} Right: {} Depth: {} All: {}", rgb_count_, left_count_, right_count_,
                              depth_count_, all_count_);
#endif
}

dai::Pipeline depthai::create_camera_pipeline() const {
#ifndef NDEBUG
    spdlog::get(name_)->debug("creating pipeline");
#endif
    dai::Pipeline p;

    // IMU
    auto imu      = p.create<dai::node::IMU>();
    auto xout_imu = p.create<dai::node::XLinkOut>();
    xout_imu->setStreamName("imu");

    // Enable raw readings at 500Hz for accel and gyro
    if (use_raw_) {
        imu->enableIMUSensor({dai::IMUSensor::ACCELEROMETER_RAW, dai::IMUSensor::GYROSCOPE_RAW}, 400);
    } else {
        imu->enableIMUSensor({dai::IMUSensor::ACCELEROMETER, dai::IMUSensor::GYROSCOPE_CALIBRATED}, 400);
    }

    // above this threshold packets will be sent in batch of X, if the host is not blocked
    imu->setBatchReportThreshold(1);
    // maximum number of IMU packets in a batch, if it's reached device will block sending until host can receive it
    // if lower or equal to batchReportThreshold then the sending is always blocking on device
    imu->setMaxBatchReports(1);
    // WARNING, temporarily 6 is the max

    // Link plugins CAM -> XLINK
    imu->out.link(xout_imu->input);

    // Color Camera, default 30 FPS
    auto color_cam = p.create<dai::node::ColorCamera>();
    auto xlink_out = p.create<dai::node::XLinkOut>();
    xlink_out->setStreamName("preview");

    color_cam->setPreviewSize(640, 480);
    color_cam->setResolution(dai::ColorCameraProperties::SensorResolution::THE_1080_P);
    color_cam->setInterleaved(true);

    // Link plugins CAM -> XLINK
    color_cam->preview.link(xlink_out->input);

    // Mono Cameras
    auto mono_left  = p.create<dai::node::MonoCamera>();
    auto mono_right = p.create<dai::node::MonoCamera>();
    // MonoCamera
    mono_left->setResolution(dai::MonoCameraProperties::SensorResolution::THE_400_P);
    mono_left->setBoardSocket(dai::CameraBoardSocket::LEFT);
    mono_left->setFps(30.0);
    mono_right->setResolution(dai::MonoCameraProperties::SensorResolution::THE_400_P);
    mono_right->setBoardSocket(dai::CameraBoardSocket::RIGHT);
    mono_right->setFps(30.0);

    // Stereo Setup
    //  Better handling for occlusions:
    bool lrcheck = true;
    // Closer-in minimum depth, disparity range is doubled (from 95 to 190):
    bool extended = false;
    // Better accuracy for longer distance, fractional disparity 32-levels:
    bool subpixel = false;

    /*
    int max_disp = 96;
    if (extended)
        max_disp *= 2;
    if (subpixel)
        max_disp *= 32; // 5 bits fractional disparity
    */
    // StereoDepth
    auto stereo       = p.create<dai::node::StereoDepth>();
    auto xout_rectifL = p.create<dai::node::XLinkOut>();
    auto xout_rectifR = p.create<dai::node::XLinkOut>();
    auto xout_depth   = p.create<dai::node::XLinkOut>();

    stereo->initialConfig.setConfidenceThreshold(200);
    stereo->setLeftRightCheck(lrcheck);
    stereo->setExtendedDisparity(extended);
    stereo->setSubpixel(subpixel);

    xout_depth->setStreamName("depth");
    xout_rectifL->setStreamName("rectified_left");
    xout_rectifR->setStreamName("rectified_right");

    stereo->rectifiedLeft.link(xout_rectifL->input);
    stereo->rectifiedRight.link(xout_rectifR->input);

    stereo->depth.link(xout_depth->input);
    // Link plugins CAM -> STEREO -> XLINK
    mono_left->out.link(stereo->left);
    mono_right->out.link(stereo->right);

    return p;
}

// This line makes the plugin importable by Spindle
PLUGIN_MAIN(depthai)
