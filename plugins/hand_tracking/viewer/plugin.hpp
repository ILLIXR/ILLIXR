#pragma once

// clang-format off
#include <GL/glew.h>    // GLEW has to be loaded before other GL libraries
#include <GLFW/glfw3.h> // Also loading first, just to be safe
// clang-format on

#include "illixr/plugin.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/hand_tracking_data.hpp"
#include "illixr/camera_data.hpp"
#include "illixr/data_format.hpp"
#include "imgui/imgui.h"

#include <eigen3/Eigen/Core>

namespace ILLIXR {
class viewer : public plugin {
public:
    viewer(const std::string& name_, phonebook* pb_);
    void start() override;
    ~viewer() override;
    void make_gui(const switchboard::ptr<const HandTracking::ht_frame>& frame);

private:
//    static void make_detection_table(const HandTracking::ht_detection& det, image::image_type it);
    void make_detection_table(units::eyes eye, int idx, const std::string& label);
    void make_position_table();

    std::shared_ptr<RelativeClock>          _clock;
    const std::shared_ptr<switchboard>      _switchboard;
    std::shared_ptr<HandTracking::ht_frame> _ht_frame;
    std::shared_ptr<pose_type>              _pose;
    std::shared_ptr<camera_data>            _camera;
    GLFWwindow*                             _viewport{};
    uint count = 0;

    GLuint          textures[2];
    Eigen::Vector2i raw_size = Eigen::Vector2i::Zero();
    Eigen::Vector2i processed_size = Eigen::Vector2i::Zero();
    Eigen::Vector2i combined_size = Eigen::Vector2i::Zero();

    bool _wc = false;   // webcam images need to be flipped
    bool _zed = false;
    Eigen::Matrix4f basicProjection;
    const HandTracking::ht_frame *current_frame;
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    bool enabled_right = false;
    std::string tab_label;
    units::eyes single_eye = units::LEFT_EYE;
    std::vector<ILLIXR::image::image_type> detections;

    std::map<uint64_t, HandTracking::ht_frame> ht_frames;
    static int requested_unit_;
    static units::measurement_unit base_unit_;
};

}
