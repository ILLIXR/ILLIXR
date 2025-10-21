#if defined(_WIN32) || defined(_WIN64)
    #include <windows.h>
#endif
// clang-format off
#include <GL/glew.h> // GLEW has to be loaded before other GL libraries
// clang-format on

#include "illixr/error_util.hpp"
#include "illixr/global_module_defs.hpp"
#include "illixr/math_util.hpp"
#include "plugin.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <eigen3/Eigen/Core>
#include <future>
#include <iostream>
#include <thread>

using namespace ILLIXR;
using namespace ILLIXR::data_format;

// Wake up 1 ms after vsync instead of exactly at vsync to account for scheduling uncertainty
static constexpr std::chrono::milliseconds VSYNC_SAFETY_DELAY{1};

[[maybe_unused]] gldemo::gldemo(const std::string& name, phonebook* pb)
    : threadloop{name, pb}
    , ext_window_{new xlib_gl_extended_window{1, 1, phonebook_->lookup_impl<xlib_gl_extended_window>()->context_}}
    , switchboard_{phonebook_->lookup_impl<switchboard>()}
    , pose_prediction_{phonebook_->lookup_impl<pose_prediction>()}
    , clock_{phonebook_->lookup_impl<relative_clock>()}
    , vsync_{switchboard_->get_reader<switchboard::event_wrapper<time_point>>("vsync_estimate")}
    , image_handle_{switchboard_->get_writer<image_handle>("image_handle")}
    , eye_buffer_{switchboard_->get_writer<rendered_frame>("eyebuffer")} {
    spdlogger(switchboard_->get_env_char("GLDEMO_LOG_LEVEL"));
}

// Essentially, a crude equivalent of XRWaitFrame.
void gldemo::wait_vsync() {
    switchboard::ptr<const switchboard::event_wrapper<time_point>> next_vsync = vsync_.get_ro_nullable();
    time_point                                                     now        = clock_->now();
    time_point                                                     wait_time{};

    if (next_vsync == nullptr) {
        // If no vsync data available, just sleep for roughly a vsync period.
        // We'll get synced back up later.
        std::this_thread::sleep_for(display_params::period);
        return;
    }

#ifndef NDEBUG
    if (log_count_ > LOG_PERIOD) {
        double vsync_in = duration_to_double<std::milli>(**next_vsync - now);
        spdlog::get(name_)->debug("First vsync is in {} ms", vsync_in);
    }
#endif

    bool has_rendered_this_interval = (now - last_time_) < display_params::period;

    // If less than one frame interval has passed since we last rendered...
    if (has_rendered_this_interval) {
        // We'll wait until the next vsync, plus a small delay time.
        // Delay time helps with some inaccuracies in scheduling.
        wait_time = **next_vsync + VSYNC_SAFETY_DELAY;

        // If our sleep target is in the past, bump it forward
        // by a vsync period, so it's always in the future.
        while (wait_time < now) {
            wait_time += display_params::period;
        }

#ifndef NDEBUG
        if (log_count_ > LOG_PERIOD) {
            double wait_in = duration_to_double<std::milli>(wait_time - now);
            spdlog::get(name_)->debug("Waiting until next vsync, in {} ms", wait_in);
        }
#endif
        // Perform the sleep.
        // TODO: Consider using Monado-style sleeping, where we nanosleep for
        // most of the wait, and then spin-wait for the rest?
        std::this_thread::sleep_for(wait_time - now);
    } else {
#ifndef NDEBUG
        if (log_count_ > LOG_PERIOD) {
            spdlog::get(name_)->debug("We haven't rendered yet, rendering immediately");
        }
#endif
    }
}

void gldemo::_p_thread_setup() {
    last_time_ = clock_->now();

    // Note: glXMakeContextCurrent must be called from the thread which will be using it.
#if defined(_WIN32) || defined(_WIN64)
    HGLRC                ctx = wglGetCurrentContext();
    HDC                  dcx = wglGetCurrentDC();
#endif
    [[maybe_unused]] int gl_result =
#if defined(_WIN32) || defined(_WIN64)
        wglMakeCurrent(ext_window_->hdc_, ext_window_->context_);
    DWORD error = GetLastError();
#else
        static_cast<bool>(glXMakeCurrent(ext_window_->display_, ext_window_->window_, ext_window_->context_));
#endif
    assert(gl_result && "glXMakeCurrent should not fail");
}

void gldemo::_p_one_iteration() {
    // Essentially, XRWaitFrame.
    wait_vsync();

    glUseProgram(demo_shader_program_);
    glBindFramebuffer(GL_FRAMEBUFFER, eye_texture_FBO_);

    glUseProgram(demo_shader_program_);
    glBindVertexArray(demo_vao_);
    glViewport(0, 0, display_params::width_pixels, display_params::height_pixels);

    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);

    glClearDepth(1);

    Eigen::Matrix4f model_matrix = Eigen::Matrix4f::Identity();

    const fast_pose_type fast_pose = pose_prediction_->get_fast_pose();
    pose_type            pose      = fast_pose.pose;

    Eigen::Matrix3f head_rotation_matrix = pose.orientation.toRotationMatrix();

    // Excessive? Maybe.
    constexpr int LEFT_EYE = 0;

    for (auto eye_idx = 0; eye_idx < 2; eye_idx++) {
        // Offset of eyeball from pose
        auto eyeball = Eigen::Vector3f((eye_idx == LEFT_EYE ? -display_params::ipd / 2.0f : display_params::ipd / 2.0f), 0, 0);

        // Apply head rotation to eyeball offset vector
        eyeball = head_rotation_matrix * eyeball;

        // Apply head position to eyeball
        eyeball += pose.position;

        // Build our eye matrix from the pose's position + orientation.
        Eigen::Matrix4f eye_matrix   = Eigen::Matrix4f::Identity();
        eye_matrix.block<3, 1>(0, 3) = eyeball; // Set position to eyeball's position
        eye_matrix.block<3, 3>(0, 0) = pose.orientation.toRotationMatrix();

        // Objects' "view matrix" is inverse of eye matrix.
        auto view_matrix = eye_matrix.inverse();

        // We'll calculate this model view matrix
        // using fresh pose data, if we have any.
        Eigen::Matrix4f model_view_matrix = view_matrix * model_matrix;
        glUniformMatrix4fv(static_cast<GLint>(model_view_), 1, GL_FALSE, (GLfloat*) (model_view_matrix.data()));
        glUniformMatrix4fv(static_cast<GLint>(projection_), 1, GL_FALSE, (GLfloat*) (basic_projection_.data()));

        glBindTexture(GL_TEXTURE_2D, eye_textures_[eye_idx]);
        glFramebufferTexture(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, eye_textures_[eye_idx], 0);
        glBindTexture(GL_TEXTURE_2D, 0);
        glClearColor(0.9f, 0.9f, 0.9f, 1.0f);

        RAC_ERRNO_MSG("gldemo before glClear");
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        RAC_ERRNO_MSG("gldemo after glClear");

        demo_scene_.Draw();
    }

    glFinish();

#ifndef NDEBUG
    const double frame_duration_s = duration_to_double(clock_->now() - last_time_);
    const double fps              = 1.0 / frame_duration_s;

    if (log_count_ > LOG_PERIOD) {
        spdlog::get(name_)->debug("Submitting frame to buffer {}, frametime: {}, FPS: {}", which_buffer_, frame_duration_s,
                                  fps);
    }
#endif
    last_time_ = clock_->now();

    /// Publish our submitted frame handle to Switchboard!
    eye_buffer_.put(eye_buffer_.allocate<rendered_frame>(rendered_frame{
        // Somehow, C++ won't let me construct this object if I remove the `rendered_frame{` and `}`.
        // `allocate<rendered_frame>(...)` _should_ forward the arguments to rendered_frame's constructor, but I guess
        // not.
        std::array<GLuint, 2>{0, 0}, std::array<GLuint, 2>{which_buffer_, which_buffer_}, fast_pose,
        fast_pose.predict_computed_time, last_time_}));

    which_buffer_ = !which_buffer_;

#ifndef NDEBUG
    if (log_count_ > LOG_PERIOD) {
        log_count_ = 0;
    } else {
        log_count_++;
    }
#endif
}

// We override start() to control our own lifecycle
void gldemo::start() {
    [[maybe_unused]] const bool gl_result_0 =
#if defined(_WIN32) || defined(_WIN64)
        static_cast<bool>(wglMakeCurrent(ext_window_->hdc_, ext_window_->context_));
#else
        static_cast<bool>(glXMakeCurrent(ext_window_->display_, ext_window_->window_, ext_window_->context_));
#endif
    assert(gl_result_0 && "glXMakeCurrent should not fail");

    // Init and verify GLEW
    const GLenum glew_err = glewInit();
    if (glew_err != GLEW_OK) {
        spdlog::get(name_)->error("GLEW Error: {}", (void*) glewGetErrorString(glew_err));
        ILLIXR::abort("Failed to initialize GLEW");
    }

    glEnable(GL_DEBUG_OUTPUT);
    glDebugMessageCallback(message_callback, nullptr);

    // Create two shared textures, one for each eye.
    create_shared_eyebuffer(&(eye_textures_[0]));
    image_handle_.put(image_handle_.allocate<image_handle>(image_handle{eye_textures_[0], 1, swapchain_usage::LEFT_SWAPCHAIN}));
    create_shared_eyebuffer(&(eye_textures_[1]));
    image_handle_.put(
        image_handle_.allocate<image_handle>(image_handle{eye_textures_[1], 1, swapchain_usage::RIGHT_SWAPCHAIN}));

    // Initialize FBO and depth targets, attaching to the frame handle
    create_FBO(&(eye_textures_[0]), &eye_texture_FBO_, &eye_texture_depth_target_);

    // Create and bind global VAO object
    glGenVertexArrays(1, &demo_vao_);
    glBindVertexArray(demo_vao_);

    demo_shader_program_ = init_and_link(demo_vertex_shader, demo_fragment_shader);
#ifndef NDEBUG
    spdlog::get(name_)->debug("Demo app shader program is program {}", demo_shader_program_);
#endif

    vertex_position_ = glGetAttribLocation(demo_shader_program_, "vertexPosition");
    vertex_normal_   = glGetAttribLocation(demo_shader_program_, "vertexNormal");
    model_view_      = glGetUniformLocation(demo_shader_program_, "u_modelview");
    projection_      = glGetUniformLocation(demo_shader_program_, "u_projection");
    color_uniform_   = glGetUniformLocation(demo_shader_program_, "u_color");

    // Load/initialize the demo scene
    const char* obj_dir = switchboard_->get_env_char("ILLIXR_DEMO_DATA");
    if (obj_dir == nullptr) {
        ILLIXR::abort("Please define ILLIXR_DEMO_DATA.");
    }

    demo_scene_ = ObjScene(std::string(obj_dir), "scene.obj");

    // Construct perspective projection matrix
    math_util::projection_fov(&basic_projection_, display_params::fov_x / 2.0f, display_params::fov_x / 2.0f,
                              display_params::fov_y / 2.0f, display_params::fov_y / 2.0f, rendering_params::near_z,
                              rendering_params::far_z);

    [[maybe_unused]] const bool gl_result_1 =
#if defined(_WIN32) || defined(_WIN64)
        static_cast<bool>(wglMakeCurrent(ext_window_->hdc_, ext_window_->context_));
#else
        static_cast<bool>(glXMakeCurrent(ext_window_->display_, None, nullptr));
#endif
    assert(gl_result_1 && "glXMakeCurrent should not fail");

    // Effectively, last vsync was at zero.
    // Try to run gldemo right away.
    threadloop::start();
}

void gldemo::create_shared_eyebuffer(GLuint* texture_handle) {
    // Create the shared eye texture handle
    glGenTextures(1, texture_handle);
    glBindTexture(GL_TEXTURE_2D, *texture_handle);

    // Set the texture parameters for the texture that the FBO will be mapped into
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, display_params::width_pixels, display_params::height_pixels, 0, GL_RGB,
                 GL_UNSIGNED_BYTE, nullptr);

    // Unbind texture
    glBindTexture(GL_TEXTURE_2D, 0);
}

void gldemo::create_FBO(const GLuint* texture_handle, GLuint* fbo, GLuint* depth_target) {
    // Create a framebuffer to draw some things to the eye texture
    glGenFramebuffers(1, fbo);

    // Bind the FBO as the active framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, *fbo);
    glGenRenderbuffers(1, depth_target);
    glBindRenderbuffer(GL_RENDERBUFFER, *depth_target);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, display_params::width_pixels, display_params::height_pixels);
    // glRenderbufferStorageMultisample(GL_RENDERBUFFER, fboSampleCount, GL_DEPTH_COMPONENT, display_params::width_pixels,
    // display_params::height_pixels);

    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    // Bind eyebuffer texture
    spdlog::get(name_)->info("About to bind eyebuffer texture, texture handle: {}", *texture_handle);

    glBindTexture(GL_TEXTURE_2D, *texture_handle);
    glFramebufferTexture(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, *texture_handle, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    // attach a renderbuffer to depth attachment point
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, *depth_target);

    // Unbind FBO
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

PLUGIN_MAIN(gldemo)
