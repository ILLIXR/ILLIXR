
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

class depthai : public plugin {
public:
    depthai(std::string name_, phonebook* pb_)
        : plugin{name_, pb_}
        , sb{pb->lookup_impl<switchboard>()}
        , _m_imu_cam{sb->get_writer<imu_cam_type>("imu_cam")}
        , _m_rgb_depth{sb->get_writer<rgb_depth_type>("rgb_depth")}
        //Initialize DepthAI pipeline and device 
        , p{createCameraPipeline()}
        , d{p}
        { 
            d.startPipeline();
            #ifndef NDEBUG
                std::cout << "Depthai pipeline started" << std::endl;
            #endif
            color = d.getOutputQueue("preview", 1, false);
            depthQueue = d.getOutputQueue("depth", 1, false);
            rectifLeftQueue = d.getOutputQueue("rectified_left", 1, false);
            rectifRightQueue = d.getOutputQueue("rectified_right", 1, false);
            imuQueue = d.getOutputQueue("imu", 1, false);
            std::function<void(void)> imu_callback = [&](){callback();};
            imuQueue->addCallback(imu_callback);
        }

    void callback(){  
        std::lock_guard<std::mutex> lock(mutex);
        //Check for available data
        bool color_go = color->has<dai::ImgFrame>();
        bool depth_go = depthQueue->has<dai::ImgFrame>();
        bool rectifL_go = rectifLeftQueue->has<dai::ImgFrame>();
        bool rectifR_go = rectifRightQueue->has<dai::ImgFrame>();
        bool imuPacket_go = imuQueue->has<dai::IMUData>();
        
        #ifndef NDEBUG
                if(color_go){
                    rgb_count++;
                }
                if(depth_go){
                    depth_count++;
                }
                if(rectifL_go){
                    left_count++;
                }
                if(rectifR_go){
                    right_count++;
                }
        #endif

        if(rectifR_go && rectifL_go && depth_go && color_go) {
            #ifndef NDEBUG
                all_count++;
            #endif
            auto colorFrame = color->tryGet<dai::ImgFrame>();
            auto depthFrame = depthQueue->tryGet<dai::ImgFrame>();
            auto rectifL = rectifLeftQueue->tryGet<dai::ImgFrame>();
            auto rectifR = rectifRightQueue->tryGet<dai::ImgFrame>();
            
            cv::Mat color = cv::Mat(colorFrame->getHeight(), colorFrame->getWidth(), CV_8UC3, colorFrame->getData().data());
            cv::Mat rgb_out{color.clone()};
            cv::Mat rectifiedLeftFrame = cv::Mat(rectifL->getHeight(), rectifL->getWidth(), CV_8UC1, rectifL->getData().data());
            cv::Mat LeftOut{rectifiedLeftFrame.clone()};
            cv::flip(LeftOut, LeftOut, 1);
            cv::Mat rectifiedRightFrame = cv::Mat(rectifR->getHeight(), rectifR->getWidth(), CV_8UC1, rectifR->getData().data());
            cv::Mat RightOut{rectifiedRightFrame.clone()};
            cv::flip(RightOut, RightOut, 1);

            cv::Mat depth = cv::Mat(depthFrame->getHeight(), depthFrame->getWidth(), CV_16UC1, depthFrame->getData().data());
            cv::Mat converted_depth;
            depth.convertTo(converted_depth, CV_32FC1, 1000.f);
            cam_type_ = cam_type {
                .img0 = cv::Mat{LeftOut},
                .img1 = cv::Mat{RightOut},
                .rgb = cv::Mat{rgb_out},
                .depth = cv::Mat{converted_depth},
                .iteration = iteration_cam,
            };
            iteration_cam++;
        }
        // Images
        std::optional<cv::Mat> img0 = std::nullopt;
        std::optional<cv::Mat> img1 = std::nullopt;
        std::optional<cv::Mat> rgb = std::nullopt;
        std::optional<cv::Mat> depth = std::nullopt;

            
        if (last_iteration_cam != cam_type_.iteration)
        {
            last_iteration_cam = cam_type_.iteration;
            img0 = cam_type_.img0;
            img1 = cam_type_.img1;
            rgb = cam_type_.rgb;
            depth = cam_type_.depth;
        }
        
        std::chrono::time_point<std::chrono::steady_clock, std::chrono::steady_clock::duration> gyroTs;
        Eigen::Vector3f la;
        Eigen::Vector3f av;
        
        if(imuPacket_go) {
            auto imuPacket = imuQueue->tryGet<dai::IMUData>();
            #ifndef NDEBUG
                if(imu_packet == 0){
                    first_packet = std::chrono::steady_clock::now();
                }
                imu_packet++;
            #endif
            
            auto imuDatas = imuPacket->imuDatas;
            for(auto& imuData : imuDatas) {
                if(useRaw){
                    gyroTs = std::chrono::time_point_cast<std::chrono::nanoseconds>(imuData.rawGyroscope.timestamp.getTimestamp());
                    if (gyroTs <= test_time_point){return;}
                    test_time_point = gyroTs;
                    la = {imuData.rawAcceleroMeter.x, imuData.rawAcceleroMeter.y, imuData.rawAcceleroMeter.z};
                    av = {imuData.rawGyroscope.x, imuData.rawGyroscope.y, imuData.rawGyroscope.z};
                }
                else {
                    gyroTs = std::chrono::time_point_cast<std::chrono::nanoseconds>(imuData.gyroscope.timestamp.getTimestamp());
                    if (gyroTs <= test_time_point){return;}
                    test_time_point = gyroTs;
                    la = {imuData.acceleroMeter.x, imuData.acceleroMeter.y, imuData.acceleroMeter.z};
                    av = {imuData.gyroscope.x, imuData.gyroscope.y, imuData.gyroscope.z};
                }
                
            }
        
            // Time as ullong (nanoseconds)
            ullong imu_time = static_cast<ullong>(gyroTs.time_since_epoch().count());

            // Time as time_point
            using time_point = std::chrono::system_clock::time_point;
            time_type imu_time_point{std::chrono::duration_cast<time_point::duration>(std::chrono::nanoseconds(imu_time))};
            
            // Submit to switchboard
            #ifndef NDEBUG
                imu_pub++;
            #endif
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
            
            if (rgb && depth)
            {
                #ifndef NDEBUG
                    rgbd_pub++;
                #endif
                _m_rgb_depth.put(_m_rgb_depth.allocate<rgb_depth_type>(
                    {
                        rgb,
                        depth,
                        imu_time
                    }
                ));
            }
        }

    }
        
    virtual ~depthai() override {
        #ifndef NDEBUG
            std::printf("Depthai Destructor: %d Packets Received %d Published: IMU: %d RGB-D: %d\n", imu_packet, imu_pub, rgbd_pub);
            auto dur = std::chrono::steady_clock::now() - first_packet;
            std::printf("Time since first packet: %ld ms\n",  std::chrono::duration_cast<std::chrono::milliseconds>(dur).count());
            std::printf("Depthai RGB: %d Left: %d Right: %d Depth: %d All: %d\n", rgb_count, left_count, right_count, depth_count, all_count);
        #endif
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
    std::chrono::time_point<std::chrono::steady_clock> first_packet;
    std::chrono::time_point<std::chrono::steady_clock, std::chrono::steady_clock::duration> test_time_point;
    bool useRaw = true;

    dai::Pipeline p;
    dai::Device d;

    std::shared_ptr<dai::DataOutputQueue> color;
    std::shared_ptr<dai::DataOutputQueue> depthQueue;
    std::shared_ptr<dai::DataOutputQueue> rectifLeftQueue;
    std::shared_ptr<dai::DataOutputQueue> rectifRightQueue;
    std::shared_ptr<dai::DataOutputQueue> imuQueue;


    dai::Pipeline createCameraPipeline() {
        #ifndef NDEBUG
            std::cout << "Depthai creating pipeline" << std::endl;
        #endif
        dai::Pipeline p;

        // IMU
        auto imu = p.create<dai::node::IMU>();
        auto xoutImu = p.create<dai::node::XLinkOut>();
        xoutImu->setStreamName("imu");

        dai::IMUSensorConfig sensorConfig;
        sensorConfig.reportIntervalUs = 2500;  // 400hz
        if(useRaw){
            sensorConfig.sensorId = dai::IMUSensorId::RAW_ACCELEROMETER;
        }
        else{
            sensorConfig.sensorId = dai::IMUSensorId::ACCELEROMETER;
        }

        imu->enableIMUSensor(sensorConfig);

        if(useRaw){
            sensorConfig.sensorId = dai::IMUSensorId::RAW_GYROSCOPE;
        }
        else{
            sensorConfig.sensorId = dai::IMUSensorId::GYROSCOPE_CALIBRATED;
        }
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
