#include "plugin.hpp"

#include <iomanip>
#include <opencv2/core/core.hpp>
#include <opencv2/opencv.hpp>

#define RGB_MODE   0
#define DEPTH_MODE 0

using namespace ILLIXR;

[[maybe_unused]] openni_plugin::openni_plugin(const std::string& name, phonebook* pb)
    : threadloop{name, pb}
    , switchboard_{phonebook_->lookup_impl<switchboard>()}
    , clock_{phonebook_->lookup_impl<relative_clock>()}
    , rgb_depth_{switchboard_->get_writer<rgb_depth_type>("rgb_depth")} {
    spdlogger(std::getenv("OPENNI_LOG_LEVEL"));
    if (!camera_initialize()) {
        spdlog::get(name)->error("Initialization failed");
        exit(0);
    }
}

openni_plugin::~openni_plugin() {
    color_.destroy();
    depth_.destroy();
}

threadloop::skip_option openni_plugin::_p_should_skip() {
    auto now  = std::chrono::steady_clock::now();
    cam_time_ = std::chrono::time_point_cast<std::chrono::milliseconds>(now).time_since_epoch().count();
    if (cam_time_ > last_timestamp_) {
        std::this_thread::sleep_for(std::chrono::milliseconds{time_sleep_});
        return skip_option::run;
    } else {
        return skip_option::skip_and_yield;
    }
}

void openni_plugin::_p_one_iteration() {
    RAC_ERRNO_MSG("openni at start of _p_one_iteration");

    // read cam data
    color_.readFrame(&color_frame_);
    depth_.readFrame(&depth_frame_);
    // get timestamp
    assert(color_frame_.getTimestamp() != depth_frame_.getTimestamp());

    // convert to cv format
    cv::Mat color_mat;
    color_mat.create(color_frame_.getHeight(), color_frame_.getWidth(), CV_8UC3);
    auto* color_buffer = (const openni::RGB888Pixel*) color_frame_.getData();
    memcpy(color_mat.data, color_buffer, 3 * color_frame_.getHeight() * color_frame_.getWidth() * sizeof(uint8_t));
    cv::cvtColor(color_mat, color_mat, cv::COLOR_BGR2BGRA);
    color_mat.convertTo(color_mat, CV_8UC4);

    cv::Mat depth_mat;
    depth_mat.create(depth_frame_.getHeight(), depth_frame_.getWidth(), CV_16UC1);
    auto* depth_buffer = (const openni::DepthPixel*) depth_frame_.getData();
    memcpy(depth_mat.data, depth_buffer, depth_frame_.getHeight() * depth_frame_.getWidth() * sizeof(uint16_t));

    assert(cam_time_);
    if (first_time_ == 0) {
        first_time_      = cam_time_;
        first_real_time_ = clock_->now();
    }
    time_point _cam_time_point{first_real_time_ + std::chrono::nanoseconds(cam_time_ - first_time_)};
    rgb_depth_.put(rgb_depth_.allocate(_cam_time_point, color_mat, depth_mat));

    last_timestamp_ = cam_time_;
    RAC_ERRNO_MSG("openni at end of _p_one_iteration");
}

bool openni_plugin::camera_initialize() {
    // initialize openni
    device_status_ = openni::OpenNI::initialize();
    if (device_status_ != openni::STATUS_OK)
        spdlog::get(name_)->error("Initialize failed: {}", openni::OpenNI::getExtendedError());

    // open device_
    device_status_ = device_.open(openni::ANY_DEVICE);
    if (device_status_ != openni::STATUS_OK)
        spdlog::get(name_)->error("Device open failed: {}", openni::OpenNI::getExtendedError());

    /*_____________________________ DEPTH ___________________________*/
    // create depth_ channel
    device_status_ = depth_.create(device_, openni::SENSOR_DEPTH);
    if (device_status_ != openni::STATUS_OK)
        spdlog::get(name_)->warn("Couldn't find depth stream:\n{}", openni::OpenNI::getExtendedError());

    // get depth_ options
    const openni::SensorInfo*               depth_info  = device_.getSensorInfo(openni::SENSOR_DEPTH);
    const openni::Array<openni::VideoMode>& modes_depth = depth_info->getSupportedVideoModes();
#ifndef NDEBUG
    for (int i = 0; i < modes_depth.getSize(); i++) {
        spdlog::get(name_)->debug("Depth Mode {}: {}x{}, {} fps, {} format", i, modes_depth[i].getResolutionX(),
                                  modes_depth[i].getResolutionY(), modes_depth[i].getFps(),
                                  modes_depth[i].getPixelFormat());
    }
#endif
    device_status_ = depth_.setVideoMode(modes_depth[DEPTH_MODE]);
    if (openni::STATUS_OK != device_status_)
        spdlog::get(name_)->error("error: depth format not supported...");
    // start depth_ stream
    device_status_ = depth_.start();
    if (device_status_ != openni::STATUS_OK)
        spdlog::get(name_)->error("Couldn't start the depth_ stream {}", openni::OpenNI::getExtendedError());

    /*_____________________________ COLOR ___________________________*/
    // create color_ channel
    device_status_ = color_.create(device_, openni::SENSOR_COLOR);
#ifndef NDEBUG
    if (device_status_ != openni::STATUS_OK)
        spdlog::get(name_)->debug("Couldn't find color stream:\n{}", openni::OpenNI::getExtendedError());
#endif

    // get color_ options
    const openni::SensorInfo*               color_info  = device_.getSensorInfo(openni::SENSOR_COLOR);
    const openni::Array<openni::VideoMode>& modes_color = color_info->getSupportedVideoModes();
#ifndef NDEBUG
    for (int i = 0; i < modes_color.getSize(); i++) {
        spdlog::get(name_)->debug("Color Mode {}: {}x{}, {} fps, {} format", i, modes_color[i].getResolutionX(),
                                  modes_color[i].getResolutionY(), modes_color[i].getFps(),
                                  modes_color[i].getPixelFormat());
    }
#endif
    device_status_ = color_.setVideoMode(modes_color[RGB_MODE]);
    if (openni::STATUS_OK != device_status_)
        spdlog::get(name_)->error("error: color format not supported...");
    // start color_ stream
    device_status_ = color_.start();
#ifndef NDEBUG
    if (device_status_ != openni::STATUS_OK)
        spdlog::get(name_)->debug("Couldn't start color stream:\n{}", openni::OpenNI::getExtendedError());
#endif
    int min_fps = std::min(modes_color[RGB_MODE].getFps(), modes_depth[DEPTH_MODE].getFps());
    time_sleep_ = static_cast<uint64_t>((1.0f / static_cast<float>(min_fps)) * 1000);

    return depth_.isValid() && color_.isValid();
}


PLUGIN_MAIN(openni_plugin)
