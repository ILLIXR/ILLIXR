
#include <cstdio>
#include <iostream>

#include "opencv2/opencv.hpp"

// Inludes common necessary includes for development using depthai library
#include "depthai/depthai.hpp"

// ILLIXR includes
#include "common/threadloop.hpp"
#include "common/switchboard.hpp"
#include "common/data_format.hpp"

using namespace ILLIXR;

/// Inherit from `plugin` if you don't need the threadloop
class depthai : public plugin {
public:
    depthai(std::string name_, phonebook* pb_)
        : plugin{name_, pb_}
        , sb{pb->lookup_impl<switchboard>()}
        , _m_imu_cam{sb->get_writer<imu_cam_type>("imu_cam")}
        , _m_rgb_depth{sb->get_writer<rgb_depth_type>("rgb_depth")}
        , p{createCameraPipeline()}
        , d{p}
        { 
            d.startPipeline();
            std::cout << "Depthai pipeline started" << std::endl;
            color = d.getOutputQueue("preview", 1, false);
            depthQueue = d.getOutputQueue("depth", 1, true);
            rectifLeftQueue = d.getOutputQueue("rectified_left", 1, false);
            rectifRightQueue = d.getOutputQueue("rectified_right", 1, false);
            imuQueue = d.getOutputQueue("imu", 50, false);
            std::function<void(void)> i_lambda = [&](){callback();};
            auto imuPacket = imuQueue->get<dai::IMUData>();
            imuQueue->addCallback(i_lambda);
            
        }

    void callback(){  
        std::lock_guard<std::mutex> lock(mutex);
        //Retrieve available data
        interrupts++;
        
        auto colorFrame = color->tryGet<dai::ImgFrame>();
        auto depthFrame = depthQueue->tryGet<dai::ImgFrame>();
        auto rectifL = rectifLeftQueue->tryGet<dai::ImgFrame>();
        auto rectifR = rectifRightQueue->tryGet<dai::ImgFrame>();
        auto imuPacket = imuQueue->tryGet<dai::IMUData>();
        if(rectifR && rectifL && depthFrame && colorFrame) {
            // std::cout << "Images Received.." << std::endl;
            cv::Mat rgb = cv::Mat(colorFrame->getHeight(), colorFrame->getWidth(), CV_8UC3, colorFrame->getData().data());
            cv::Mat rectifiedLeftFrame = cv::Mat(rectifL->getHeight(), rectifL->getWidth(), CV_8UC1, rectifL->getData().data());
            cv::flip(rectifiedLeftFrame, rectifiedLeftFrame, 1);
            cv::Mat rectifiedRightFrame = cv::Mat(rectifR->getHeight(), rectifR->getWidth(), CV_8UC1, rectifR->getData().data());
            cv::flip(rectifiedRightFrame, rectifiedRightFrame, 1);
            cv::Mat depth = cv::Mat(depthFrame->getHeight(), depthFrame->getWidth(), CV_16UC1, depthFrame->getData().data());
            cv::Mat converted_depth;
            depth.convertTo(converted_depth, CV_32FC1, 1000.f);
            cam_type_ = cam_type {
                .img0 = cv::Mat{rectifiedLeftFrame},
                .img1 = cv::Mat{rectifiedRightFrame},
                .rgb = cv::Mat{rgb},
                .depth = cv::Mat{converted_depth},
                .iteration = iteration_cam,
            };
            iteration_cam++;
        }
        // Images
        std::optional<cv::Mat> img0 = std::nullopt;
        std::optional<cv::Mat> img1 = std::nullopt;
        std::optional<cv::Mat> rgbi = std::nullopt;
        std::optional<cv::Mat> depthi = std::nullopt;

            
        if (last_iteration_cam != cam_type_.iteration)
        {
            last_iteration_cam = cam_type_.iteration;
            img0 = cam_type_.img0;
            img1 = cam_type_.img1;
            rgbi = cam_type_.rgb;
            depthi = cam_type_.depth;
        }
        
        std::chrono::time_point<std::chrono::steady_clock, std::chrono::steady_clock::duration> gyroTs;
        Eigen::Vector3f la;
        Eigen::Vector3f av;
        
        if(imuPacket) {
            // std::cout << "ImuPacket Received.." << std::endl;
            if(imu_packet == 0){
                first_packet = std::chrono::steady_clock::now();
            }
            imu_packet++;
            auto imuDatas = imuPacket->imuDatas;
            time_type test_time_point;
            for(auto& imuData : imuDatas) {
                gyroTs = std::chrono::time_point_cast<std::chrono::nanoseconds>(imuData.gyroscope.timestamp.getTimestamp());
                if (gyroTs == imu_time_point){return;}
                la = {imuData.acceleroMeter.x, imuData.acceleroMeter.y, imuData.acceleroMeter.z};
                av = {imuData.gyroscope.x, imuData.gyroscope.y, imuData.gyroscope.z};
            }
        
            // Time as ullong (nanoseconds)
            ullong imu_time = static_cast<ullong>(gyroTs.time_since_epoch().count());

            // Time as time_point
            using time_point = std::chrono::system_clock::time_point;
            time_type imu_time_point{std::chrono::duration_cast<time_point::duration>(std::chrono::nanoseconds(imu_time))};
            
            // Submit to switchboard
            // std::cout << "Publishing IMU and stereo.." << std::endl;
            imu_pub++;
            _m_imu_cam.put(_m_imu_cam.allocate<imu_cam_type>(
                {
                    imu_time_point,
                    av,
                    la,
                    img0,
                    img1,
                    imu_time
                }
            ));
            
            if (rgbi && depthi)
            {
                // std::cout << "Publishing RGB-D.." << std::endl;
                rgbd_pub++;
                _m_rgb_depth.put(_m_rgb_depth.allocate<rgb_depth_type>(
                    {
                        rgbi,
                        depthi,
                        imu_time
                    }
                ));
            }
        }

    }
        



    
    virtual ~depthai() override {
        std::printf("Depthai Destructor Interrupts: %d Packets %d PUB: IMU: %d RGB-D: %d\n", interrupts, imu_packet, imu_pub, rgbd_pub);
        auto dur = std::chrono::steady_clock::now() - first_packet;
        std::printf("Time since first packet: %ld ms\n",  std::chrono::duration_cast<std::chrono::milliseconds>(dur).count());
    }


private:
    const std::shared_ptr<switchboard> sb;
    switchboard::writer<imu_cam_type> _m_imu_cam;
	switchboard::writer<rgb_depth_type> _m_rgb_depth;
    std::mutex mutex;
    typedef struct {
        cv::Mat img0;
        cv::Mat img1;
        cv::Mat rgb;
        cv::Mat depth;
        int iteration;
    } cam_type;


    cam_type cam_type_;

    int iteration_cam = 0;
	// int iteration_imu = 0;
	int last_iteration_cam;
	// int last_iteration_imu;
    int imu_packet{0};
    int interrupts{0};
    int imu_pub{0};
    int rgbd_pub{0};
    std::chrono::time_point<std::chrono::steady_clock> first_packet;


    dai::Pipeline p;
    dai::Device d;

    std::shared_ptr<dai::DataOutputQueue> color;
    std::shared_ptr<dai::DataOutputQueue> depthQueue;
    std::shared_ptr<dai::DataOutputQueue> rectifLeftQueue;
    std::shared_ptr<dai::DataOutputQueue> rectifRightQueue;
    std::shared_ptr<dai::DataOutputQueue> imuQueue;


    dai::Pipeline createCameraPipeline() {
        std::cout << "Depthai creating pipeline" << std::endl;
        dai::Pipeline p;

        // IMU
        auto imu = p.create<dai::node::IMU>();
        auto xoutImu = p.create<dai::node::XLinkOut>();
        xoutImu->setStreamName("imu");

        dai::IMUSensorConfig sensorConfig;
        sensorConfig.reportIntervalUs = 2500;  // 400hz
        sensorConfig.sensorId = dai::IMUSensorId::IMU_ACCELEROMETER;
        imu->enableIMUSensor(sensorConfig);
        sensorConfig.sensorId = dai::IMUSensorId::IMU_GYROSCOPE_CALIBRATED;
        imu->enableIMUSensor(sensorConfig);
        // above this threshold packets will be sent in batch of X, if the host is not blocked
        imu->setBatchReportThreshold(1);
        // maximum number of IMU packets in a batch, if it's reached device will block sending until host can receive it
        // if lower or equal to batchReportThreshold then the sending is always blocking on device
        imu->setMaxBatchReports(1);
        // WARNING, temporarily 6 is the max

        // Link plugins CAM -> XLINK
        imu->out.link(xoutImu->input);

        //Color Camera, default 30 FPS
        auto colorCam = p.create<dai::node::ColorCamera>();
        auto xlinkOut = p.create<dai::node::XLinkOut>();
        xlinkOut->setStreamName("preview");

        colorCam->setPreviewSize(640, 480); 
        colorCam->setResolution(dai::ColorCameraProperties::SensorResolution::THE_1080_P);
        colorCam->setInterleaved(true);

        // Link plugins CAM -> XLINK
        colorCam->preview.link(xlinkOut->input);

        //Mono Cameras
        auto monoLeft = p.create<dai::node::MonoCamera>();
        auto monoRight = p.create<dai::node::MonoCamera>();
        // MonoCamera
        monoLeft->setResolution(dai::MonoCameraProperties::SensorResolution::THE_400_P);
        monoLeft->setBoardSocket(dai::CameraBoardSocket::LEFT);
        monoLeft->setFps(30.0);
        monoRight->setResolution(dai::MonoCameraProperties::SensorResolution::THE_400_P);
        monoRight->setBoardSocket(dai::CameraBoardSocket::RIGHT);
        monoRight->setFps(30.0);
        
        //Stereo Setup
        bool outputDepth = true;
        bool outputRectified = true;
        // Better handling for occlusions:
        bool lrcheck = true;
        // Closer-in minimum depth, disparity range is doubled (from 95 to 190):
        bool extended = false;
        // Better accuracy for longer distance, fractional disparity 32-levels:
        bool subpixel = false;

        int maxDisp = 96;
        if(extended) maxDisp *= 2;
        if(subpixel) maxDisp *= 32;  // 5 bits fractional disparity
        // StereoDepth
        auto stereo = p.create<dai::node::StereoDepth>();
        auto xoutRectifL = p.create<dai::node::XLinkOut>();
        auto xoutRectifR = p.create<dai::node::XLinkOut>();
        auto xoutDepth = p.create<dai::node::XLinkOut>();
        
        stereo->setOutputDepth(outputDepth);
        stereo->setOutputRectified(outputRectified);
        stereo->setConfidenceThreshold(200);
        stereo->setLeftRightCheck(lrcheck);
        stereo->setExtendedDisparity(extended);
        stereo->setSubpixel(subpixel);

        xoutDepth->setStreamName("depth");
        xoutRectifL->setStreamName("rectified_left");
        xoutRectifR->setStreamName("rectified_right");

        if(outputRectified) {
            stereo->rectifiedLeft.link(xoutRectifL->input);
            stereo->rectifiedRight.link(xoutRectifR->input);
        }
        stereo->depth.link(xoutDepth->input);
        // Link plugins CAM -> STEREO -> XLINK
        monoLeft->out.link(stereo->left);
        monoRight->out.link(stereo->right);

        return p;
    }

    
};

// This line makes the plugin importable by Spindle
PLUGIN_MAIN(depthai);
