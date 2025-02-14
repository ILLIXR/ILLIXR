#pragma once

// clang-format off
#include <GL/glew.h>    // GLEW has to be loaded before other GL libraries
#include <GLFW/glfw3.h> // Also loading first, just to be safe
// clang-format on

#include "illixr/data_format/camera_data.hpp"
#include "illixr/data_format/hand_tracking_data.hpp"
#include "illixr/imgui/imgui.h"
#include "illixr/plugin.hpp"
#include "illixr/switchboard.hpp"

#include <eigen3/Eigen/Core>

namespace ILLIXR {
class viewer : public plugin {
public:
    viewer(const std::string& name_, phonebook* pb_);
    ~viewer() override;
    void start() override;
    void make_gui(const switchboard::ptr<const data_format::ht::ht_frame>& frame);

private:
    void make_detection_table(data_format::units::eyes eye, int idx, const std::string& label) const;
    void make_position_table() const;

    std::shared_ptr<relative_clock>            clock_;
    const std::shared_ptr<switchboard>         switchboard_;
    std::shared_ptr<data_format::ht::ht_frame> ht_frame_;
    const data_format::pose_data               pose_;
#ifdef VIEW_DUMP
    std::map<data_format::ht::hand, data_format::ht::hand_points> _true_hand_positions;
#endif

    GLFWwindow* viewport_{};

    GLuint          textures_[2];
    Eigen::Vector2i raw_size       = Eigen::Vector2i::Zero();
    Eigen::Vector2i processed_size = Eigen::Vector2i::Zero();
    Eigen::Vector2i combined_size  = Eigen::Vector2i::Zero();

    bool                             wc_  = false; // webcam images need to be flipped
    bool                             zed_ = false;
    Eigen::Matrix4f                  basicProjection_;
    const data_format::ht::ht_frame* current_frame_;
    ImVec4                           clear_color_ = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    bool                                                enabled_right_ = false;
    std::string                                         tab_label_;
    data_format::units::eyes                            single_eye_ = data_format::units::LEFT_EYE;
    std::vector<ILLIXR::data_format::image::image_type> detections_;

    std::map<uint64_t, data_format::ht::ht_frame> ht_frames_;
    static int                                    requested_unit_;
    static data_format::units::measurement_unit   base_unit_;
};

} // namespace ILLIXR
