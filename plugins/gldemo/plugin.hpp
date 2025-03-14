#pragma once

#include "illixr/data_format/frame.hpp"
#include "illixr/data_format/misc.hpp"
#include "illixr/data_format/pose_prediction.hpp"
#include "illixr/extended_window.hpp"
#include "illixr/gl_util/obj.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/shader_util.hpp"
#include "illixr/shaders/demo_shader.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"

namespace ILLIXR {

#ifndef NDEBUG
size_t       log_count_ = 0;
const size_t LOG_PERIOD = 20;
#endif

class gldemo : public threadloop {
public:
    // Public constructor, create_component passes Switchboard handles ("plugs")
    // to this constructor. In turn, the constructor fills in the private
    // references to the switchboard plugs, so the component can read the
    // data whenever it needs to.
    [[maybe_unused]] gldemo(const std::string& name, phonebook* pb);
    void wait_vsync();
    void _p_thread_setup() override;
    void _p_one_iteration() override;
    void start() override;

private:
    static void create_shared_eyebuffer(GLuint* texture_handle);
    void        create_FBO(const GLuint* texture_handle, GLuint* fbo, GLuint* depth_target);

    const std::unique_ptr<const xlib_gl_extended_window>              ext_window_;
    const std::shared_ptr<switchboard>                                switchboard_;
    const std::shared_ptr<data_format::pose_prediction>               pose_prediction_;
    const std::shared_ptr<const relative_clock>                       clock_;
    const switchboard::reader<switchboard::event_wrapper<time_point>> vsync_;

    // Switchboard plug for application eye buffer.
    // We're not "writing" the actual buffer data,
    // we're just atomically writing the handle to the
    // correct eye/framebuffer in the "swapchain".
    switchboard::writer<data_format::image_handle>   image_handle_;
    switchboard::writer<data_format::rendered_frame> eye_buffer_;

    GLuint eye_textures_[2]{};
    GLuint eye_texture_FBO_{};
    GLuint eye_texture_depth_target_{};

    unsigned char which_buffer_ = 0;

    GLuint demo_vao_{};
    GLuint demo_shader_program_{};

    [[maybe_unused]] GLuint vertex_position_{};
    [[maybe_unused]] GLuint vertex_normal_{};
    GLuint                  model_view_{};
    GLuint                  projection_{};

    [[maybe_unused]] GLuint color_uniform_{};

    ObjScene demo_scene_;

    Eigen::Matrix4f basic_projection_;

    time_point last_time_{};
};
} // namespace ILLIXR
