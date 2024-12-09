#pragma once

#include <eigen3/Eigen/Dense>

#include "include/zed_opencv.hpp"

#include "illixr/camera_data.hpp"
#include "illixr/switchboard.hpp"

#define UNITS MILLIMETER

namespace ILLIXR {
class zed_camera : public sl::Camera {
public:
    explicit zed_camera(const std::shared_ptr<switchboard>& sb)
        : sl::Camera()
        , switchboard_{sb}
        , frame{sl::REFERENCE_FRAME::WORLD} {} //(switchboard_->get_env_bool("USE_WCS")) ? sl::REFERENCE_FRAME::WORLD : sl::REFERENCE_FRAME::CAMERA} { }

    sl::ERROR_CODE open(const sl::InitParameters& params);

    [[nodiscard]] const sl::Transform& get_initial_position() const {
        return initial_position_;
    }

    [[nodiscard]] const camera_data& get_config() const {
        return config_;
    };

    [[nodiscard]] Eigen::Vector3f get_translation() const {
        const auto temp = initial_position_.getTranslation();
        return {temp[0], temp[1], temp[2]};
    }

    [[nodiscard]] Eigen::Quaternionf get_orientation() const {
        const auto temp = initial_position_.getOrientation();
        return {temp[0], temp[1], temp[2], temp[3]};
    }

    sl::POSITIONAL_TRACKING_STATE getPosition(sl::Pose& pose) {
        return sl::Camera::getPosition(pose, frame);
    }

    [[nodiscard]] float getBaseline() const {
        return config_.baseline;
    }

private:
    const std::shared_ptr<switchboard> switchboard_;
    sl::Transform                      initial_position_;
    camera_data                        config_;
    sl::REFERENCE_FRAME                frame;
};
}
