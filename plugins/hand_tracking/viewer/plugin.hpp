#pragma once

// clang-format off
#include <GL/glew.h>    // GLEW has to be loaded before other GL libraries
#include <GLFW/glfw3.h> // Also loading first, just to be safe
// clang-format on

#include "illixr/plugin.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/hand_tracking_data.hpp"
#include "imgui/imgui.h"

#include <eigen3/Eigen/Core>

namespace ILLIXR {
class viewer : public plugin {
public:
    viewer(const std::string& name_, phonebook* pb_);
    void start() override;
    ~viewer() override;
    void make_gui(const switchboard::ptr<const ht_frame>& frame);

private:
    static void make_detection_table(const ht_detection& det, const image::image_type it);

    std::shared_ptr<RelativeClock>     _clock;
    const std::shared_ptr<switchboard> _switchboard;
    std::shared_ptr<ht_frame>          _ht_frame;
    GLFWwindow*                        _viewport{};
    uint count = 0;

    GLuint          textures[6];
    Eigen::Vector2i raw_size = Eigen::Vector2i::Zero();
    Eigen::Vector2i processed_size = Eigen::Vector2i::Zero();
    Eigen::Vector2i combined_size = Eigen::Vector2i::Zero();
    bool _zed = false;  // zed images have to be turned into RGB
    bool _wc = false;   // webcam images need to be flipped
    Eigen::Matrix4f basicProjection;
    const ht_frame *current_frame;
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
};

}
