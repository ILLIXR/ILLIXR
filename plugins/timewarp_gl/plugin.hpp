#pragma once

// clang-format off
#include <GL/glew.h> // must come first
#include <GL/gl.h>
// clang-format on

#include "illixr/extended_window.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/pose_prediction.hpp"
#include "illixr/relative_clock.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"
#include "utils/hmd.hpp"

namespace ILLIXR {

typedef threadloop timewarp_type;

class timewarp_gl : public timewarp_type {
public:
    // Public constructor, create_component passes Switchboard handles ("plugs")
    // to this constructor. In turn, the constructor fills in the private
    // references to the switchboard plugs, so the component can read the
    // data whenever it needs to.
    timewarp_gl(const std::string& name, phonebook* pb);
    void _setup();
    void _prepare_rendering();
    void warp(const switchboard::ptr<const rendered_frame>& most_recent_frame);
#ifndef ILLIXR_MONADO
    skip_option _p_should_skip() override;
    void        _p_thread_setup() override;
    void        _p_one_iteration() override;
#endif

private:
    GLubyte*      read_texture_image();
    static GLuint convert_vk_format_to_GL(int64_t vk_format);
    void          import_vulkan_image(const vk_image_handle& vk_handle, swapchain_usage usage);
    void          build_timewarp(HMD::hmd_info_t& hmd_info);
    static void   calculate_time_warp_transform(Eigen::Matrix4f& transform, const Eigen::Matrix4f& render_projection_matrix,
                                                const Eigen::Matrix4f& render_view_matrix,
                                                const Eigen::Matrix4f& new_view_matrix);
#ifndef ILLIXR_MONADO
    [[nodiscard]] time_point                get_next_swap_time_estimate() const;
    [[maybe_unused]] [[nodiscard]] duration estimate_time_to_sleep(const double frame_percentage) const;
#endif

    const std::shared_ptr<switchboard>          switchboard_;
    const std::shared_ptr<pose_prediction>      pose_prediction_;
    const std::shared_ptr<const relative_clock> clock_;

    // OpenGL objects
    Display*   display_;
    Window     root_window_;
    GLXContext context_;

    // Shared objects between ILLIXR and the application (either gldemo or Monado)
    bool              rendering_ready_;
    graphics_api      client_backend_;
    std::atomic<bool> image_handles_ready_{};

    // Left and right eye images
    std::array<std::vector<image_handle>, 2> eye_image_handles_;
    std::array<std::vector<GLuint>, 2>       eye_swapchains_;
    std::array<size_t, 2>                    eye_swapchains_size_{};

    // Intermediate timewarp framebuffers for left and right eye textures
    std::array<GLuint, 2> eye_output_textures_{};
    std::array<GLuint, 2> eye_framebuffers_{};

#ifdef ILLIXR_MONADO
    std::array<image_handle, 2> eye_output_handles_;

    // Synchronization helper for Monado
    switchboard::writer<signal_to_quad> signal_quad_;

    // When using Monado, timewarp is a plugin and not a threadloop, but we still keep track of the iteration number
    std::size_t iteration_no = 0;
#else
    // Note: 0.9 works fine without hologram, but we need a larger safety net with hologram enabled
    static constexpr double DELAY_FRACTION = 0.9;

    // Switchboard plug for application eye buffer.
    switchboard::reader<rendered_frame> eyebuffer_;

    // Switchboard plug for publishing vsync estimates
    switchboard::writer<switchboard::event_wrapper<time_point>> vsync_estimate_;

    // Switchboard plug for publishing offloaded data
    switchboard::writer<texture_pose> offload_data_;
    // Timewarp only has vsync estimates with native-gl
    record_coalescer mtp_logger_;
#endif

    record_coalescer timewarp_gpu_logger_;

    // Switchboard plug for sending hologram calls
    switchboard::writer<hologram_input> hologram_;

    GLuint timewarp_shader_program_{};

    time_point time_last_swap_{};

    HMD::hmd_info_t hmd_info_{};

    // Eye sampler array
    GLuint                  eye_sampler_0_{};
    [[maybe_unused]] GLuint eye_sampler_1_{};

    // Eye index uniform
    [[maybe_unused]] GLuint tw_eye_index_uniform_{};

    // VAOs
    GLuint tw_vao{};

    // Position and UV attribute locations
    GLuint distortion_pos_attr_{};
    GLuint distortion_uv0_attr_{};
    GLuint distortion_uv1_attr_{};
    GLuint distortion_uv2_attr_{};

    // Distortion mesh information
    GLuint num_distortion_vertices_{};
    GLuint num_distortion_indices_{};

    // Distortion mesh CPU buffers and GPU VBO handles
    std::vector<HMD::mesh_coord3d_t> distortion_positions_;
    GLuint                           distortion_positions_vbo_{};
    std::vector<GLuint>              distortion_indices_;
    GLuint                           distortion_indices_vbo_{};
    std::vector<HMD::uv_coord_t>     distortion_uv0_;
    GLuint                           distortion_uv0_vbo_{};
    std::vector<HMD::uv_coord_t>     distortion_uv1_;
    GLuint                           distortion_uv1_vbo_{};
    std::vector<HMD::uv_coord_t>     distortion_uv2_;
    GLuint                           distortion_uv2_vbo_{};

    // Handles to the start and end timewarp
    // transform matrices (3x4 uniforms)
    GLuint tw_start_transform_uniform_{};
    GLuint tw_end_transform_uniform_{};
    // bool uniform to check if the Y axis needs to be inverted
    GLuint flip_y_uniform_{};
    // Basic perspective projection matrix
    Eigen::Matrix4f basic_projection_;

    // Hologram call data
    ullong hologram_seq_{0};
    ullong signal_quad_seq_{0};

    bool disable_warp_;

    bool enable_offload_;

    // PBO buffer for reading texture image
    GLuint PBO_buffer_{};

    duration offload_duration_{};

    size_t log_count_  = 0;
    size_t LOG_PERIOD_ = 20;
};

} // namespace ILLIXR
