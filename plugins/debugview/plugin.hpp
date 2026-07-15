#pragma once

#include <eigen3/Eigen/Core>
#include <eigen3/Eigen/Geometry>
#if defined(_WIN32) || defined(_WIN64)
    #include <windows.h>
#endif

// clang-format off
#include <GL/glew.h>    // GLEW has to be loaded before other GL libraries
#include <GLFW/glfw3.h> // Also loading first, just to be safe
// clang-format on

#include "illixr/data_format/imu.hpp"
#include "illixr/data_format/opencv_data_types.hpp"
#include "illixr/data_format/pose_prediction.hpp"
#include "illixr/gl_util/obj.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"

constexpr size_t TEST_PATTERN_WIDTH  = 256;
constexpr size_t TEST_PATTERN_HEIGHT = 256;

namespace ILLIXR {
class debugview : public threadloop {
public:
    // Public constructor, Spindle passes the phonebook to this
    // constructor. In turn, the constructor fills in the private
    // references to the switchboard plugs, so the plugin can read
    // the data whenever it needs to.

    [[maybe_unused]] debugview(const std::string& name, phonebook* pb);

    void draw_GUI();

    bool load_camera_images();

    bool load_rgb_depth();

    static Eigen::Matrix4f generate_headset_transform(const Eigen::Vector3f& position, const Eigen::Quaternionf& rotation,
                                                      const Eigen::Vector3f& position_offset);

    void _p_thread_setup() override;

    void _p_one_iteration() override;

    /* compatibility interface */
    // Debug view application overrides _p_start to control its own lifecycle/scheduling.
    void start() override;

    ~debugview() override;

private:
    // GLFWwindow * const glfw_context;
    const std::shared_ptr<switchboard>                  switchboard_;
    const std::shared_ptr<data_format::pose_prediction> pose_prediction_;

    const bool display_backend_manages_glfw_;

    switchboard::reader<data_format::pose_type>                   slow_pose_reader_;
    switchboard::reader<data_format::imu_raw_type>                fast_pose_reader_;
    switchboard::reader<data_format::rgb_depth_type>              rgb_depth_reader_;
    switchboard::buffered_reader<data_format::binocular_cam_type> cam_reader_;

    GLFWwindow* gui_window_{};

    [[maybe_unused]] uint8_t test_pattern_[TEST_PATTERN_WIDTH][TEST_PATTERN_HEIGHT]{};

    Eigen::Vector3d view_euler_          = Eigen::Vector3d::Zero();
    Eigen::Vector2d last_mouse_position_ = Eigen::Vector2d::Zero();
    Eigen::Vector2d mouse_velocity_      = Eigen::Vector2d::Zero();
    bool            being_dragged_       = false;

    float view_distance_ = 2.0;

    bool follow_headset_ = true;

    Eigen::Vector3f tracking_position_offset_ = Eigen::Vector3f{0.0f, 0.0f, 0.0f};

    switchboard::ptr<const data_format::binocular_cam_type> cam_;
    switchboard::ptr<const data_format::rgb_depth_type>     rgb_depth_;
    bool                                                    use_cam_       = false;
    bool                                                    use_rgb_depth_ = false;

    GLuint                           camera_texture_[2]{};
    Eigen::Vector2i                  camera_texture_size_[2] = {Eigen::Vector2i::Zero(), Eigen::Vector2i::Zero()};
    GLuint                           rgb_depth_texture_[2]{};
    [[maybe_unused]] Eigen::Vector2i rgbd_texture_size_[2] = {Eigen::Vector2i::Zero(), Eigen::Vector2i::Zero()};

    GLuint demo_vao_{};
    GLuint demo_shader_program_{};

    [[maybe_unused]] GLuint vertex_pos_attr{};
    [[maybe_unused]] GLuint vertex_normal_attr_{};
    GLuint                  model_view_attr_{};
    GLuint                  projection_attr_{};

    [[maybe_unused]] GLuint color_uniform_{};

    ObjScene demo_scene_;
    ObjScene headset_;

    Eigen::Matrix4f basic_projection_;
};

} // namespace ILLIXR
