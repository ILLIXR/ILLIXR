#include "../common/threadloop.hpp"
#include "../common/phonebook.hpp"
#include "../common/switchboard.hpp"
#include "../common/data_format.hpp"
#include "../common/relative_clock.hpp"
#include "../common/pose_prediction.hpp"

#include <OpenNI.h>
#include <iomanip>
#include <opencv/cv.hpp>
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>

using namespace ILLIXR;

class openni_plugin : public ILLIXR::threadloop {
public:
    openni_plugin(std::string name_, phonebook* pb_)
        : threadloop{name_, pb_}
        , sb{pb->lookup_impl<switchboard>()}
        , _m_clock{pb->lookup_impl<RelativeClock>()}
        , _m_rgb_depth{sb->get_writer<rgb_depth_type>("rgb_depth")}
    {
        if (!camera_initialize()) {
            std::cout << "Initialization failed" << std::endl;
            exit(0);
        }
    }
    virtual ~openni_plugin() override {
        color.destroy();
        depth.destroy();
    }
protected:
    virtual skip_option _p_should_skip() override {
        // read cam data
        color.readFrame(&colorFrame);
        depth.readFrame(&depthFrame);
        // get timestamp
        assert(colorFrame.getTimestamp() != depthFrame.getTimestamp());
        cam_time = colorFrame.getTimestamp()*1000;

        if (cam_time > last_ts) {
            std::this_thread::sleep_for(std::chrono::milliseconds{2});
            return skip_option::run;
        } else {
            return skip_option::skip_and_yield;
        }
    }
    virtual void _p_one_iteration() override {
        RAC_ERRNO_MSG("zed at start of _p_one_iteration");
        
        // convert to cv format
        cv::Mat colorMat;
        colorMat.create(colorFrame.getHeight(), colorFrame.getWidth(), CV_8UC3);
        const openni::RGB888Pixel* colorBuffer = (const openni::RGB888Pixel*)colorFrame.getData();
        memcpy( colorMat.data, colorBuffer, 3*colorFrame.getHeight()*colorFrame.getWidth()*sizeof(uint8_t) );
        cv::cvtColor(colorMat, colorMat, cv::COLOR_BGR2BGRA);
        colorMat.convertTo(colorMat, CV_8UC4);

        cv::Mat depthMat;
        depthMat.create(depthFrame.getHeight(), depthFrame.getWidth(), CV_16UC1);
        const openni::DepthPixel* depthBuffer = (const openni::DepthPixel*)depthFrame.getData();
        memcpy( depthMat.data, depthBuffer, depthFrame.getHeight()*depthFrame.getWidth()*sizeof(uint16_t) );

        assert(cam_time);
        if (_m_first_time == 0) {
            _m_first_time  = cam_time;
            _m_first_real_time = _m_clock->now();
        }
        time_point cam_time_point{_m_first_real_time + std::chrono::nanoseconds(cam_time - _m_first_time)};
        _m_rgb_depth.put(_m_rgb_depth.allocate(cam_time_point, colorMat, depthMat));

        last_ts = colorFrame.getTimestamp()*1000;
        RAC_ERRNO_MSG("zed_cam at end of _p_one_iteration");
    }

    bool camera_initialize() {
        // initialize openni
        rc = openni::OpenNI::initialize();
        if (rc != openni::STATUS_OK) 
            std::cout << "Initialize failed: " << openni::OpenNI::getExtendedError() << std::endl;

        // open device
        rc = device.open(openni::ANY_DEVICE);
        if (rc != openni::STATUS_OK) 
            std::cout << "Device open failed: " << openni::OpenNI::getExtendedError() << std::endl;

        /*_____________________________ DEPTH ___________________________*/
        // create depth channel
        rc = depth.create(device, openni::SENSOR_DEPTH);
        if (rc != openni::STATUS_OK) 
            printf("Couldn't find depth stream:\n%s\n", openni::OpenNI::getExtendedError());

#ifndef NDEBUG
        // get depth options
        const openni::SensorInfo* depthInfo = device.getSensorInfo(openni::SENSOR_DEPTH); 
        const openni::Array< openni::VideoMode>& modesDepth = depthInfo->getSupportedVideoModes();
        for (int i = 0; i<modesDepth.getSize(); i++) {
            printf("Depth Mode %i: %ix%i, %i fps, %i format\n", i, modesDepth[i].getResolutionX(), modesDepth[i].getResolutionY(),
                modesDepth[i].getFps(), modesDepth[i].getPixelFormat()); 
        }
        rc = depth.setVideoMode(modesDepth[4]);
        if (openni::STATUS_OK != rc)
            std::cout << "error: depth fromat not supprted..." << std::endl;
#endif
        // start depth stream
        rc = depth.start();
        if (rc != openni::STATUS_OK)
            std::cout << "Couldn't start the depth stream" << openni::OpenNI::getExtendedError() << std::endl;
        
        /*_____________________________ COLOR ___________________________*/
        // create color channel 
        rc = color.create(device, openni::SENSOR_COLOR);
        if (rc != openni::STATUS_OK) 
            printf("Couldn't find color stream:\n%s\n", openni::OpenNI::getExtendedError());

#ifndef NDEBUG
        // get color options 
        const openni::SensorInfo* colorInfo = device.getSensorInfo(openni::SENSOR_COLOR); 
        const openni::Array< openni::VideoMode>& modesColor = colorInfo->getSupportedVideoModes();
        for (int i = 0; i<modesColor.getSize(); i++) {
            printf("Color Mode %i: %ix%i, %i fps, %i format\n", i, modesColor[i].getResolutionX(), modesColor[i].getResolutionY(),
                modesColor[i].getFps(), modesColor[i].getPixelFormat()); 
        }
        rc = color.setVideoMode(modesColor[9]);
        if (openni::STATUS_OK != rc)
            std::cout << "error: color format not supprted..." << std::endl;
#endif
        // start color stream
        rc = color.start();
        if (rc != openni::STATUS_OK) 
            printf("Couldn't start color stream:\n%s\n", openni::OpenNI::getExtendedError());   

        return depth.isValid() && color.isValid();
    }
private:
    // ILLIXR
    const std::shared_ptr<switchboard> sb;
    const std::shared_ptr<const RelativeClock> _m_clock;
    switchboard::writer<rgb_depth_type> _m_rgb_depth;

    // OpenNI
    openni::Status rc = openni::STATUS_OK;
    openni::Device device;
	openni::VideoStream depth, color;
    openni::VideoFrameRef depthFrame, colorFrame;
    cv::VideoCapture capture;

    // timestamp
    uint64_t cam_time;
    uint64_t last_ts = 0;
    uint64_t _m_first_time;
    time_point _m_first_real_time;
};
PLUGIN_MAIN(openni_plugin);