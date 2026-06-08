#include "zed_camera.hpp"

#include "illixr/data_format/poses/pose_base.hpp"

// Set exposure to 8% of camera frame time. This is an empirically determined number
static constexpr unsigned EXPOSURE_TIME_PERCENT = 40;

using namespace ILLIXR;
using namespace ILLIXR::data_format;

sl::ERROR_CODE zed_camera::open(const sl::InitParameters& params) {
    sl::ERROR_CODE err = sl::Camera::open(params);
    if (err != sl::ERROR_CODE::SUCCESS) {
        return err;
    }
    auto trans  = switchboard_->root_coordinates.position();
    auto orient = switchboard_->root_coordinates.orientation();

    initial_position_.setTranslation(sl::Translation(trans.x(), trans.y(), trans.z()));
    initial_position_.setOrientation(sl::Orientation({orient.x(), orient.y(), orient.z(), orient.w()}));

    auto                 cam_conf  = getCameraInformation().camera_configuration;
    sl::CameraParameters left_cam  = cam_conf.calibration_parameters.left_cam;
    sl::CameraParameters right_cam = cam_conf.calibration_parameters.right_cam;
    config_                        = {
        static_cast<size_t>(cam_conf.resolution.width),
        static_cast<size_t>(cam_conf.resolution.height),
        cam_conf.fps,
        cam_conf.calibration_parameters.getCameraBaseline(),
        {{data_format::pose::side::LEFT, 
                                 {left_cam.cx, left_cam.cy, left_cam.v_fov * (M_PI / 180.f), left_cam.h_fov * (M_PI / 180.f)}},
         {data_format::pose::side::RIGHT,
                                 {right_cam.cx, right_cam.cy, right_cam.v_fov * (M_PI / 180.f), right_cam.h_fov * (M_PI / 180.f)}}}};

    sl::PositionalTrackingParameters tracking_params(initial_position_);
    err = enablePositionalTracking(tracking_params);
    if (err != sl::ERROR_CODE::SUCCESS) {
        spdlog::get("illixr")->info("[zed] {}", toString(err).c_str());
        close();
    }

    return err;
}
