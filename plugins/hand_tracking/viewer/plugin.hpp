#pragma once

// clang-format off
#include <GL/glew.h>    // GLEW has to be loaded before other GL libraries
#include <GLFW/glfw3.h> // Also loading first, just to be safe
// clang-format on

#include "illixr/plugin.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/data_format/hand_tracking_data.hpp"
#include "illixr/data_format/camera_data.hpp"
#include "imgui/imgui.h"

#include <eigen3/Eigen/Core>

namespace ILLIXR {
class viewer : public plugin {
public:
    viewer(const std::string& name_, phonebook* pb_);
    void start() override;
    ~viewer() override;
    void make_gui(const switchboard::ptr<const data_format::ht::ht_frame>& frame);

private:
    void make_detection_table(data_format::units::eyes eye, int idx, const std::string& label) const;
    void make_position_table() const;

    std::shared_ptr<RelativeClock>                       _clock;
    const std::shared_ptr<switchboard>                   _switchboard;
    std::shared_ptr<data_format::ht::ht_frame>           _ht_frame;
    const data_format::pose_data                         _pose;
    std::map<data_format::ht::hand, data_format::ht::hand_points> _true_hand_positions;

    GLFWwindow*                                          _viewport{};

    GLuint          textures[2];
    Eigen::Vector2i raw_size = Eigen::Vector2i::Zero();
    Eigen::Vector2i processed_size = Eigen::Vector2i::Zero();
    Eigen::Vector2i combined_size = Eigen::Vector2i::Zero();

    bool _wc = false;   // webcam images need to be flipped
    bool _zed = false;
    Eigen::Matrix4f basicProjection;
    const data_format::ht::ht_frame *current_frame;
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    bool enabled_right = false;
    std::string tab_label;
    data_format::units::eyes single_eye = data_format::units::LEFT_EYE;
    std::vector<ILLIXR::data_format::image::image_type> detections;

    std::map<uint64_t, data_format::ht::ht_frame> ht_frames;
    static int requested_unit_;
    static data_format::units::measurement_unit base_unit_;
};

}
