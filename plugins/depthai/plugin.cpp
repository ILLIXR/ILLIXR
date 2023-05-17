#include <cstdio>
#include <iostream>
#include <opencv2/opencv.hpp>

// Inludes common necessary includes for development using depthai library
#include <depthai/depthai.hpp>

// ILLIXR includes
#include "illixr/data_format.hpp"
#include "illixr/relative_clock.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"

using namespace ILLIXR;

class depthai : public plugin {
public:
    depthai(std::string name_, phonebook* pb_)
        : plugin{name_, pb_}
        , sb{pb->lookup_impl<switchboard>()}
        , _m_clock{pb->lookup_impl<RelativeClock>()}
        , _m_imu{sb->get_writer<imu_type>("imu")}
        , _m_cam{sb->get_writer<cam_type>("cam")}
        , _m_rgb_depth{sb->get_writer<rgb_depth_type>("rgb_depth")} // Initialize DepthAI pipeline and device
        , device{createCameraPipeline()} {
#ifndef NDEBUG
        std::cout << "Depthai pipeline started" << std::endl;
#endif
        colorQueue                             = device.getOutputQueue("preview", 1, false);
        depthQueue                             = device.getOutputQueue("depth", 1, false);
        rectifLeftQueue                        = device.getOutputQueue("rectified_left", 1, false);
        rectifRightQueue                       = device.getOutputQueue("rectified_right", 1, false);
        imuQueue                               = device.getOutputQueue("imu", 1, false);
        std::function<void(void)> imu_callback = [&]() {
            callback();
        };
        imuQueue->addCallback(imu_callback);
        test_time_point = std::chrono::steady_clock::now();
    }

    void callback() {
        std::lock_guard<std::mutex> lock(mutex);
        // Check for available data
        bool color_go     = colorQueue->has<dai::ImgFrame>();
        bool depth_go     = depthQueue->has<dai::ImgFrame>();
        bool rectifL_go   = rectifLeftQueue->has<dai::ImgFrame>();
        bool rectifR_go   = rectifRightQueue->has<dai::ImgFrame>();
        bool imuPacket_go = imuQueue->has<dai::IMUData>();

#ifndef NDEBUG
        if (color_go) {
            rgb_count++;
        }
        if (depth_go) {
            depth_count++;
        }
        if (rectifL_go) {
            left_count++;
        }
        if (rectifR_go) {
            right_count++;
        }
#endif

        if (rectifR_go && rectifL_go && depth_go && color_go) {
#ifndef NDEBUG
            all_count++;
#endif
            auto colorFrame = colorQueue->tryGet<dai::ImgFrame>();
            auto depthFrame = depthQueue->tryGet<dai::ImgFrame>();
            auto rectifL    = rectifLeftQueue->tryGet<dai::ImgFrame>();
            auto rectifR    = rectifRightQueue->tryGet<dai::ImgFrame>();

            ullong cam_time = static_cast<ullong>(
                std::chrono::time_point_cast<std::chrono::nanoseconds>(colorFrame->getTimestamp()).time_since_epoch().count());
            if (!_m_first_cam_time) {
                _m_first_cam_time      = cam_time;
                _m_first_real_time_cam = _m_clock->now();
            }

            time_point cam_time_point{*_m_first_real_time_cam + std::chrono::nanoseconds(cam_time - *_m_first_cam_time)};

            cv::Mat color = cv::Mat(colorFrame->getHeight(), colorFrame->getWidth(), CV_8UC3, colorFrame->getData().data());
            cv::Mat rgb_out{color.clone()};
            cv::Mat rectifiedLeftFrame = cv::Mat(rectifL->getHeight(), rectifL->getWidth(), CV_8UC1, rectifL->getData().data());
            cv::Mat LeftOut{rectifiedLeftFrame.clone()};
            cv::flip(LeftOut, LeftOut, 1);
            cv::Mat rectifiedRightFrame =
                cv::Mat(rectifR->getHeight(), rectifR->getWidth(), CV_8UC1, rectifR->getData().data());
            cv::Mat RightOut{rectifiedRightFrame.clone()};
            cv::flip(RightOut, RightOut, 1);

            cv::Mat depth = cv::Mat(depthFrame->getHeight(), depthFrame->getWidth(), CV_16UC1, depthFrame->getData().data());
            cv::Mat converted_depth;
            depth.convertTo(converted_depth, CV_32FC1, 1000.f);

            _m_cam.put(_m_cam.allocate<cam_type>({cam_time_point, cv::Mat{LeftOut}, cv::Mat{RightOut}}));
            _m_rgb_depth.put(
                _m_rgb_depth.allocate<rgb_depth_type>({cam_time_point, cv::Mat{rgb_out}, cv::Mat{converted_depth}}));
        }

        std::chrono::time_point<std::chrono::steady_clock, std::chrono::steady_clock::duration> gyroTs;
        Eigen::Vector3d                                                                         la;
        Eigen::Vector3d                                                                         av;

        if (imuPacket_go) {
            auto imuPacket = imuQueue->tryGet<dai::IMUData>();
#ifndef NDEBUG
            if (imu_packet == 0) {
                first_packet_time = std::chrono::steady_clock::now();
            }
            imu_packet++;
#endif

            auto imuDatas = imuPacket->packets;
            for (auto& imuData : imuDatas) {
                gyroTs = std::chrono::time_point_cast<std::chrono::nanoseconds>(imuData.gyroscope.timestamp.get());
                if (gyroTs <= test_time_point) {
                    return;
                }
                test_time_point = gyroTs;
                la              = {imuData.acceleroMeter.x, imuData.acceleroMeter.y, imuData.acceleroMeter.z};
                av              = {imuData.gyroscope.x, imuData.gyroscope.y, imuData.gyroscope.z};
            }

            // Time as ullong (nanoseconds)
            ullong imu_time = static_cast<ullong>(gyroTs.time_since_epoch().count());
            if (!_m_first_imu_time) {
                _m_first_imu_time  = imu_time;
                _m_first_real_time = _m_clock->now();
            }

            time_point imu_time_point{*_m_first_real_time + std::chrono::nanoseconds(imu_time - *_m_first_imu_time)};

// Submit to switchboard
#ifndef NDEBUG
            imu_pub++;
#endif
            _m_imu.put(_m_imu.allocate<imu_type>({
                imu_time_point,
                av,
                la,
            }));
        }
    }

    virtual ~depthai() override {
#ifndef NDEBUG
        std::printf("Depthai Destructor: Packets Received %d Published: IMU: %d RGB-D: %d\n", imu_packet, imu_pub, rgbd_pub);
        auto dur = std::chrono::steady_clock::now() - first_packet_time;
        std::printf("Time since first packet: %ld ms\n", std::chrono::duration_cast<std::chrono::milliseconds>(dur).count());
        std::printf("Depthai RGB: %d Left: %d Right: %d Depth: %d All: %d\n", rgb_count, left_count, right_count, depth_count,
                    all_count);
#endif
    }

private:
    const std::shared_ptr<switchboard>         sb;
    const std::shared_ptr<const RelativeClock> _m_clock;
    switchboard::writer<imu_type>              _m_imu;
    switchboard::writer<cam_type>              _m_cam;
    switchboard::writer<rgb_depth_type>        _m_rgb_depth;
    std::mutex                                 mutex;

#ifndef NDEBUG
    int imu_packet{0};
    int imu_pub{0};
    int rgbd_pub{0};
    int rgb_count{0};
    int left_count{0};
    int right_count{0};
    int depth_count{0};
    int all_count{0};
#endif
    std::chrono::time_point<std::chrono::steady_clock>                                      first_packet_time;
    std::chrono::time_point<std::chrono::steady_clock, std::chrono::steady_clock::duration> test_time_point;
    bool                                                                                    useRaw = false;
    dai::Device                                                                             device;

    std::shared_ptr<dai::DataOutputQueue> colorQueue;
    std::shared_ptr<dai::DataOutputQueue> depthQueue;
    std::shared_ptr<dai::DataOutputQueue> rectifLeftQueue;
    std::shared_ptr<dai::DataOutputQueue> rectifRightQueue;
    std::shared_ptr<dai::DataOutputQueue> imuQueue;

    std::optional<ullong>     _m_first_imu_time;
    std::optional<time_point> _m_first_real_time;

    std::optional<ullong>     _m_first_cam_time;
    std::optional<time_point> _m_first_real_time_cam;

    dai::Pipeline createCameraPipeline() {
#ifndef NDEBUG
        std::cout << "Depthai creating pipeline" << std::endl;
#endif
        dai::Pipeline p;

        // IMU
        auto imu     = p.create<dai::node::IMU>();
        auto xoutImu = p.create<dai::node::XLinkOut>();
        xoutImu->setStreamName("imu");

        // Enable raw readings at 500Hz for accel and gyro
        if (useRaw) {
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
        imu->out.link(xoutImu->input);

        // Color Camera, default 30 FPS
        auto colorCam = p.create<dai::node::ColorCamera>();
        auto xlinkOut = p.create<dai::node::XLinkOut>();
        xlinkOut->setStreamName("preview");

        colorCam->setPreviewSize(640, 480);
        colorCam->setResolution(dai::ColorCameraProperties::SensorResolution::THE_1080_P);
        colorCam->setInterleaved(true);

        // Link plugins CAM -> XLINK
        colorCam->preview.link(xlinkOut->input);

        // Mono Cameras
        auto monoLeft  = p.create<dai::node::MonoCamera>();
        auto monoRight = p.create<dai::node::MonoCamera>();
        // MonoCamera
        monoLeft->setResolution(dai::MonoCameraProperties::SensorResolution::THE_400_P);
        monoLeft->setBoardSocket(dai::CameraBoardSocket::LEFT);
        monoLeft->setFps(30.0);
        monoRight->setResolution(dai::MonoCameraProperties::SensorResolution::THE_400_P);
        monoRight->setBoardSocket(dai::CameraBoardSocket::RIGHT);
        monoRight->setFps(30.0);

        // Stereo Setup
        //  Better handling for occlusions:
        bool lrcheck = true;
        // Closer-in minimum depth, disparity range is doubled (from 95 to 190):
        bool extended = false;
        // Better accuracy for longer distance, fractional disparity 32-levels:
        bool subpixel = false;

        int maxDisp = 96;
        if (extended)
            maxDisp *= 2;
        if (subpixel)
            maxDisp *= 32; // 5 bits fractional disparity
        // StereoDepth
        auto stereo      = p.create<dai::node::StereoDepth>();
        auto xoutRectifL = p.create<dai::node::XLinkOut>();
        auto xoutRectifR = p.create<dai::node::XLinkOut>();
        auto xoutDepth   = p.create<dai::node::XLinkOut>();

        stereo->setConfidenceThreshold(200);
        stereo->setLeftRightCheck(lrcheck);
        stereo->setExtendedDisparity(extended);
        stereo->setSubpixel(subpixel);

        xoutDepth->setStreamName("depth");
        xoutRectifL->setStreamName("rectified_left");
        xoutRectifR->setStreamName("rectified_right");

        stereo->rectifiedLeft.link(xoutRectifL->input);
        stereo->rectifiedRight.link(xoutRectifR->input);

        stereo->depth.link(xoutDepth->input);
        // Link plugins CAM -> STEREO -> XLINK
        monoLeft->out.link(stereo->left);
        monoRight->out.link(stereo->right);

        return p;
    }
};

// This line makes the plugin importable by Spindle
PLUGIN_MAIN(depthai);
