#include "../common/data_format.hpp"
#include "../common/phonebook.hpp"
#include "../common/relative_clock.hpp"
#include "../common/switchboard.hpp"
#include "../common/threadloop.hpp"

#include <iomanip>
#include <opencv/cv.hpp>
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <OpenNI.h>

#define RGB_MODE 0
#define DEPTH_MODE 0

using namespace ILLIXR;

class openni_plugin : public ILLIXR::threadloop {
public:
    openni_plugin(std::string name_, phonebook* pb_)
        : threadloop{name_, pb_}
        , sb{pb->lookup_impl<switchboard>()}
        , _m_clock{pb->lookup_impl<RelativeClock>()}
        , _m_rgb_depth{sb->get_writer<rgb_depth_type>("rgb_depth")} {
        if (!camera_initialize()) {
            std::cout << "Initialization failed" << std::endl;
            exit(0);
        }
    }

    virtual ~openni_plugin() override {
        _color.destroy();
        _depth.destroy();
    }

protected:
    virtual skip_option _p_should_skip() override {
        auto now = std::chrono::steady_clock::now();
        _cam_time = std::chrono::time_point_cast<std::chrono::milliseconds>(now).time_since_epoch().count();
        if (_cam_time > _last_ts) {
            std::this_thread::sleep_for(std::chrono::milliseconds{_time_sleep});
            return skip_option::run;
        } else {
            return skip_option::skip_and_yield;
        }
    }

    virtual void _p_one_iteration() override {
        RAC_ERRNO_MSG("openni at start of _p_one_iteration");

        // read cam data
        _color.readFrame(&_color_frame);
        _depth.readFrame(&_depth_frame);
        // get timestamp
        assert(_color_frame.getTimestamp() != _depth_frame.getTimestamp());

        // convert to cv format
        cv::Mat colorMat;
        colorMat.create(_color_frame.getHeight(), _color_frame.getWidth(), CV_8UC3);
        const openni::RGB888Pixel* colorBuffer = (const openni::RGB888Pixel*) _color_frame.getData();
        memcpy(colorMat.data, colorBuffer, 3 * _color_frame.getHeight() * _color_frame.getWidth() * sizeof(uint8_t));
        cv::cvtColor(colorMat, colorMat, cv::COLOR_BGR2BGRA);
        colorMat.convertTo(colorMat, CV_8UC4);

        cv::Mat depthMat;
        depthMat.create(_depth_frame.getHeight(), _depth_frame.getWidth(), CV_16UC1);
        const openni::DepthPixel* depthBuffer = (const openni::DepthPixel*) _depth_frame.getData();
        memcpy(depthMat.data, depthBuffer, _depth_frame.getHeight() * _depth_frame.getWidth() * sizeof(uint16_t));

        assert(_cam_time);
        if (_m_first_time == 0) {
            _m_first_time      = _cam_time;
            _m_first_real_time = _m_clock->now();
        }
        time_point _cam_time_point{_m_first_real_time + std::chrono::nanoseconds(_cam_time - _m_first_time)};
        _m_rgb_depth.put(_m_rgb_depth.allocate(_cam_time_point, colorMat, depthMat));

        _last_ts = _cam_time;
        RAC_ERRNO_MSG("openni at end of _p_one_iteration");
    }

    bool camera_initialize() {
        // initialize openni
        _device_status = openni::OpenNI::initialize();
        if (_device_status != openni::STATUS_OK)
            std::cout << "Initialize failed: " << openni::OpenNI::getExtendedError() << std::endl;

        // open _device
        _device_status = _device.open(openni::ANY_DEVICE);
        if (_device_status != openni::STATUS_OK)
            std::cout << "Device open failed: " << openni::OpenNI::getExtendedError() << std::endl;

        /*_____________________________ DEPTH ___________________________*/
        // create _depth channel
        _device_status = _depth.create(_device, openni::SENSOR_DEPTH);
        if (_device_status != openni::STATUS_OK)
            printf("Couldn't find depth stream:\n%s\n", openni::OpenNI::getExtendedError());

        // get _depth options
        const openni::SensorInfo*               depthInfo  = _device.getSensorInfo(openni::SENSOR_DEPTH);
        const openni::Array<openni::VideoMode>& modesDepth = depthInfo->getSupportedVideoModes();
        for (int i = 0; i < modesDepth.getSize(); i++) {
            printf("Depth Mode %i: %ix%i, %i fps, %i format\n", i, modesDepth[i].getResolutionX(),
                   modesDepth[i].getResolutionY(), modesDepth[i].getFps(), modesDepth[i].getPixelFormat());
        }
        _device_status = _depth.setVideoMode(modesDepth[DEPTH_MODE]);
        if (openni::STATUS_OK != _device_status)
            std::cout << "error: depth fromat not supprted..." << std::endl;
        // start _depth stream
        _device_status = _depth.start();
        if (_device_status != openni::STATUS_OK)
            std::cout << "Couldn't start the _depth stream" << openni::OpenNI::getExtendedError() << std::endl;

        /*_____________________________ COLOR ___________________________*/
        // create _color channel
        _device_status = _color.create(_device, openni::SENSOR_COLOR);
        if (_device_status != openni::STATUS_OK)
            printf("Couldn't find color stream:\n%s\n", openni::OpenNI::getExtendedError());

        // get _color options
        const openni::SensorInfo*               colorInfo  = _device.getSensorInfo(openni::SENSOR_COLOR);
        const openni::Array<openni::VideoMode>& modesColor = colorInfo->getSupportedVideoModes();
        for (int i = 0; i < modesColor.getSize(); i++) {
            printf("Color Mode %i: %ix%i, %i fps, %i format\n", i, modesColor[i].getResolutionX(),
                   modesColor[i].getResolutionY(), modesColor[i].getFps(), modesColor[i].getPixelFormat());
        }
        _device_status = _color.setVideoMode(modesColor[RGB_MODE]);
        if (openni::STATUS_OK != _device_status)
            std::cout << "error: color format not supprted..." << std::endl;
        // start _color stream
        _device_status = _color.start();
        if (_device_status != openni::STATUS_OK)
            printf("Couldn't start color stream:\n%s\n", openni::OpenNI::getExtendedError());

        int min_fps = std::min(modesColor[RGB_MODE].getFps(), modesDepth[DEPTH_MODE].getFps());
        _time_sleep = static_cast<uint64_t>((1.0f / min_fps)*1000);
        
        return _depth.isValid() && _color.isValid();
    }

private:
    // ILLIXR
    const std::shared_ptr<switchboard>         sb;
    const std::shared_ptr<const RelativeClock> _m_clock;
    switchboard::writer<rgb_depth_type>        _m_rgb_depth;

    // OpenNI
    openni::Status        _device_status = openni::STATUS_OK;
    openni::Device        _device;
    openni::VideoStream   _depth, _color;
    openni::VideoFrameRef _depth_frame, _color_frame;

    // timestamp
    uint64_t   _cam_time;
    uint64_t   _last_ts = 0;
    uint64_t   _m_first_time;
    time_point _m_first_real_time;
    uint64_t   _time_sleep;
};

PLUGIN_MAIN(openni_plugin);
