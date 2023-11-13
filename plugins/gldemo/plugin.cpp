
#include "plugin.hpp"
#include "illixr/math_util.hpp"
#include "illixr/shader_util.hpp"
#include "illixr/shaders/demo_shader.hpp"

using namespace ILLIXR;

// Wake up 1 ms after vsync instead of exactly at vsync to account for scheduling uncertainty

gldemo::gldemo(const std::string& name_, phonebook* pb_)
        : threadloop{name_, pb_}
        , xwin{new xlib_gl_extended_window{1, 1, pb->lookup_impl<xlib_gl_extended_window>()->glc}}
        , sb{pb->lookup_impl<switchboard>()}
        , pp{pb->lookup_impl<pose_prediction>()}
        , _m_clock{pb->lookup_impl<RelativeClock>()}
        , _m_vsync{sb->get_reader<switchboard::event_wrapper<time_point>>("vsync_estimate")}
        , _m_image_handle{sb->get_writer<image_handle>("image_handle")}
        , _m_eyebuffer{sb->get_writer<rendered_frame>("eyebuffer")} {
    spdlogger(std::getenv("GLDEMO_LOG_LEVEL"));
}

// Essentially, a crude equivalent of XRWaitFrame.
void gldemo::wait_vsync() {
    switchboard::ptr<const switchboard::event_wrapper<time_point>> next_vsync = _m_vsync.get_ro_nullable();
    time_point                                                     now        = _m_clock->now();
    time_point                                                     wait_time{};

    if (next_vsync == nullptr) {
        // If no vsync data available, just sleep for roughly a vsync period.
        // We'll get synced back up later.
        std::this_thread::sleep_for(display_params::period);
        return;
    }

#ifndef NDEBUG
    if (log_count > LOG_PERIOD) {
        double vsync_in = duration2double<std::milli>(**next_vsync - now);
        spdlog::get(name)->debug("First vsync is in {} ms", vsync_in);
    }
#endif

    bool hasRenderedThisInterval = (now - lastTime) < display_params::period;

    // If less than one frame interval has passed since we last rendered...
    if (hasRenderedThisInterval) {
        // We'll wait until the next vsync, plus a small delay time.
        // Delay time helps with some inaccuracies in scheduling.
        wait_time = **next_vsync + VSYNC_SAFETY_DELAY;

        // If our sleep target is in the past, bump it forward
        // by a vsync period, so it's always in the future.
        while (wait_time < now) {
            wait_time += display_params::period;
        }

#ifndef NDEBUG
        if (log_count > LOG_PERIOD) {
            double wait_in = duration2double<std::milli>(wait_time - now);
            spdlog::get(name)->debug("Waiting until next vsync, in {} ms", wait_in);
        }
#endif
        // Perform the sleep.
        // TODO: Consider using Monado-style sleeping, where we nanosleep for
        // most of the wait, and then spin-wait for the rest?
        std::this_thread::sleep_for(wait_time - now);
    } else {
#ifndef NDEBUG
        if (log_count > LOG_PERIOD) {
            spdlog::get(name)->debug("We haven't rendered yet, rendering immediately");
        }
#endif
    }
}

void gldemo::_p_thread_setup() {
    lastTime = _m_clock->now();

    // Note: glXMakeContextCurrent must be called from the thread which will be using it.
    [[maybe_unused]] const bool gl_result = static_cast<bool>(glXMakeCurrent(xwin->dpy, xwin->win, xwin->glc));
    assert(gl_result && "glXMakeCurrent should not fail");
}

void gldemo::_p_one_iteration() {
    // Essentially, XRWaitFrame.
    wait_vsync();

    glUseProgram(demoShaderProgram);
    glBindFramebuffer(GL_FRAMEBUFFER, eyeTextureFBO);

    glUseProgram(demoShaderProgram);
    glBindVertexArray(demo_vao);
    glViewport(0, 0, display_params::width_pixels, display_params::height_pixels);

    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);

    glClearDepth(1);

    Eigen::Matrix4f modelMatrix = Eigen::Matrix4f::Identity();

    const fast_pose_type fast_pose = pp->get_fast_pose();
    pose_type            pose      = fast_pose.pose;

    Eigen::Matrix3f head_rotation_matrix = pose.orientation.toRotationMatrix();

    // Excessive? Maybe.
    constexpr int LEFT_EYE = 0;

    for (auto eye_idx = 0; eye_idx < 2; eye_idx++) {
        // Offset of eyeball from pose
        auto eyeball =
                Eigen::Vector3f((eye_idx == LEFT_EYE ? -display_params::ipd / 2.0f : display_params::ipd / 2.0f), 0, 0);

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
        Eigen::Matrix4f modelViewMatrix = view_matrix * modelMatrix;
        glUniformMatrix4fv(static_cast<GLint>(modelViewAttr), 1, GL_FALSE, (GLfloat*) (modelViewMatrix.data()));
        glUniformMatrix4fv(static_cast<GLint>(projectionAttr), 1, GL_FALSE, (GLfloat*) (basicProjection.data()));

        glBindTexture(GL_TEXTURE_2D, eyeTextures[eye_idx]);
        glFramebufferTexture(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, eyeTextures[eye_idx], 0);
        glBindTexture(GL_TEXTURE_2D, 0);
        glClearColor(0.9f, 0.9f, 0.9f, 1.0f);

        RAC_ERRNO_MSG("gldemo before glClear");
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        RAC_ERRNO_MSG("gldemo after glClear");

        demoscene.Draw();
    }

    glFinish();

#ifndef NDEBUG
    const double frame_duration_s = duration2double(_m_clock->now() - lastTime);
    const double fps              = 1.0 / frame_duration_s;

    if (log_count > LOG_PERIOD) {
        spdlog::get(name)->debug("Submitting frame to buffer {}, frametime: {}, FPS: {}", which_buffer, frame_duration_s,
                                 fps);
    }
#endif
    lastTime = _m_clock->now();

    /// Publish our submitted frame handle to Switchboard!
    _m_eyebuffer.put(_m_eyebuffer.allocate<rendered_frame>(rendered_frame{
        // Somehow, C++ won't let me construct this object if I remove the `rendered_frame{` and `}`.
        // `allocate<rendered_frame>(...)` _should_ forward the arguments to rendered_frame's constructor, but I guess
        // not.
            std::array<GLuint, 2>{0, 0}, std::array<GLuint, 2>{which_buffer, which_buffer}, fast_pose,
            fast_pose.predict_computed_time, lastTime}));

    which_buffer = !which_buffer;

#ifndef NDEBUG
    if (log_count > LOG_PERIOD) {
        log_count = 0;
    } else {
        log_count++;
    }
#endif
}


//////PRIVATE
void gldemo::createSharedEyebuffer(GLuint* texture_handle) {
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

void gldemo::createFBO(const GLuint* texture_handle, GLuint* fbo, GLuint* depth_target) {
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
    spdlog::get(name)->info("About to bind eyebuffer texture, texture handle: {}", *texture_handle);

    glBindTexture(GL_TEXTURE_2D, *texture_handle);
    glFramebufferTexture(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, *texture_handle, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    // attach a renderbuffer to depth attachment point
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, *depth_target);

    // Unbind FBO
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}


// We override start() to control our own lifecycle
void gldemo::start() {
    [[maybe_unused]] const bool gl_result_0 = static_cast<bool>(glXMakeCurrent(xwin->dpy, xwin->win, xwin->glc));
    assert(gl_result_0 && "glXMakeCurrent should not fail");

    // Init and verify GLEW
    const GLenum glew_err = glewInit();
    if (glew_err != GLEW_OK) {
        spdlog::get(name)->error("GLEW Error: {}", glewGetErrorString(glew_err));
        ILLIXR::abort("Failed to initialize GLEW");
    }

    glEnable(GL_DEBUG_OUTPUT);
    glDebugMessageCallback(MessageCallback, nullptr);

    // Create two shared textures, one for each eye.
    createSharedEyebuffer(&(eyeTextures[0]));
    _m_image_handle.put(
            _m_image_handle.allocate<image_handle>(image_handle{eyeTextures[0], 1, swapchain_usage::LEFT_SWAPCHAIN}));
    createSharedEyebuffer(&(eyeTextures[1]));
    _m_image_handle.put(
            _m_image_handle.allocate<image_handle>(image_handle{eyeTextures[1], 1, swapchain_usage::RIGHT_SWAPCHAIN}));

    // Initialize FBO and depth targets, attaching to the frame handle
    createFBO(&(eyeTextures[0]), &eyeTextureFBO, &eyeTextureDepthTarget);

    // Create and bind global VAO object
    glGenVertexArrays(1, &demo_vao);
    glBindVertexArray(demo_vao);

    demoShaderProgram = init_and_link(demo_vertex_shader, demo_fragment_shader);
#ifndef NDEBUG
    spdlog::get(name)->debug("Demo app shader program is program {}", demoShaderProgram);
#endif

    vertexPosAttr    = glGetAttribLocation(demoShaderProgram, "vertexPosition");
    vertexNormalAttr = glGetAttribLocation(demoShaderProgram, "vertexNormal");
    modelViewAttr    = glGetUniformLocation(demoShaderProgram, "u_modelview");
    projectionAttr   = glGetUniformLocation(demoShaderProgram, "u_projection");
    colorUniform     = glGetUniformLocation(demoShaderProgram, "u_color");

    // Load/initialize the demo scene
    char* obj_dir = std::getenv("ILLIXR_DEMO_DATA");
    if (obj_dir == nullptr) {
        ILLIXR::abort("Please define ILLIXR_DEMO_DATA.");
    }

    demoscene = ObjScene(std::string(obj_dir), "scene.obj");

    // Construct perspective projection matrix
    math_util::projection_fov(&basicProjection, display_params::fov_x / 2.0f, display_params::fov_x / 2.0f,
                              display_params::fov_y / 2.0f, display_params::fov_y / 2.0f, rendering_params::near_z,
                              rendering_params::far_z);

    [[maybe_unused]] const bool gl_result_1 = static_cast<bool>(glXMakeCurrent(xwin->dpy, None, nullptr));
    assert(gl_result_1 && "glXMakeCurrent should not fail");

    // Effectively, last vsync was at zero.
    // Try to run gldemo right away.
    threadloop::start();
}


PLUGIN_MAIN(gldemo)
