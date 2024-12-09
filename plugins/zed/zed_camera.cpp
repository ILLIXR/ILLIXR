#include "zed_camera.hpp"

// Set exposure to 8% of camera frame time. This is an empirically determined number
static constexpr unsigned EXPOSURE_TIME_PERCENT = 40;

using namespace ILLIXR;

sl::ERROR_CODE zed_camera::open(const sl::InitParameters& params) {
    sl::ERROR_CODE err = sl::Camera::open(params);
    if (err != sl::ERROR_CODE::SUCCESS) {
        return err;
    }
    sl::Translation trans{0., 0., 0.};
    sl::Orientation orient{{1., 0., 0., 0.}};
    if (frame == sl::REFERENCE_FRAME::WORLD) {
        std::string ini_pose = switchboard_->get_env("WCF_ORIGIN");

        std::stringstream  iss(ini_pose);
        std::string        token;
        std::vector<float> ip;
        while (!iss.eof() && std::getline(iss, token, ',')) {
            ip.emplace_back(std::stof(token));
        }
        if (ip.size() == 3) {
            trans[0] = ip[0];
            trans[1] = ip[1];
            trans[2] = ip[2];
        } else if (ip.size() == 4) {
            orient[0] = ip[0];
            orient[1] = ip[1];
            orient[2] = ip[2];
            orient[3] = ip[3];
        } else if (ip.size() == 7) {
            trans[0] = ip[0];
            trans[1] = ip[1];
            trans[2] = ip[2];
            orient[0] = ip[3];
            orient[1] = ip[4];
            orient[2] = ip[5];
            orient[3] = ip[6];
        } else {
            spdlog::get("illixr")->error("[zed] Improper initial position, should have 3, 4 or 7 elements not {}", ip.size());
        }
    }
    initial_position_.setTranslation(trans);
    initial_position_.setOrientation(orient);

    auto cam_conf = getCameraInformation().camera_configuration;
    sl::CameraParameters left_cam = cam_conf.calibration_parameters.left_cam;
    sl::CameraParameters right_cam = cam_conf.calibration_parameters.right_cam;
    config_ = {cam_conf.resolution.width, cam_conf.resolution.height, cam_conf.fps, cam_conf.calibration_parameters.getCameraBaseline(),
               ILLIXR::units::UNITS, {{units::eyes::LEFT_EYE, {left_cam.cx,
                                                               left_cam.cy,
                                                               left_cam.v_fov * (M_PI / 180.),
                                                               left_cam.h_fov * (M_PI / 180.)}},
                                      {units::eyes::RIGHT_EYE, {right_cam.cx,
                                                                right_cam.cy,
                                                                right_cam.v_fov * (M_PI / 180.),
                                                                right_cam.h_fov * (M_PI / 180.)}}}};

    sl::PositionalTrackingParameters tracking_params;
    tracking_params.initial_world_transform = initial_position_;
    err = enablePositionalTracking(tracking_params);
    if (err != sl::ERROR_CODE::SUCCESS) {
        spdlog::get("illixr")->info("[zed] {}", toString(err).c_str());
        close();
    }

    return err;
}
