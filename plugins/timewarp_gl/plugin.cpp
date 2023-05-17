// clang-format off
#include <GL/glew.h> // GLEW has to be loaded before other GL libraries
// clang-format on

#include "illixr/data_format.hpp"
#include "illixr/error_util.hpp"
#include "illixr/extended_window.hpp"
#include "illixr/global_module_defs.hpp"
#include "illixr/math_util.hpp"
#include "illixr/pose_prediction.hpp"
#include "illixr/shader_util.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"
#include "shaders/basic_shader.hpp"
#include "shaders/timewarp_shader.hpp"
#include "utils/hmd.hpp"

#include <chrono>
#include <future>
#include <iostream>
#include <thread>
#include <vector>

using namespace ILLIXR;

typedef void (*glXSwapIntervalEXTProc)(Display* dpy, GLXDrawable drawable, int interval);

const record_header timewarp_gpu_record{"timewarp_gpu",
                                        {
                                            {"iteration_no", typeid(std::size_t)},
                                            {"wall_time_start", typeid(time_point)},
                                            {"wall_time_stop", typeid(time_point)},
                                            {"gpu_time_duration", typeid(std::chrono::nanoseconds)},
                                        }};

const record_header mtp_record{"mtp_record",
                               {
                                   {"iteration_no", typeid(std::size_t)},
                                   {"vsync", typeid(time_point)},
                                   {"imu_to_display", typeid(std::chrono::nanoseconds)},
                                   {"predict_to_display", typeid(std::chrono::nanoseconds)},
                                   {"render_to_display", typeid(std::chrono::nanoseconds)},
                               }};

class timewarp_gl : public threadloop {
public:
    // Public constructor, create_component passes Switchboard handles ("plugs")
    // to this constructor. In turn, the constructor fills in the private
    // references to the switchboard plugs, so the component can read the
    // data whenever it needs to.
    timewarp_gl(std::string name_, phonebook* pb_)
        : threadloop{name_, pb_}
        , sb{pb->lookup_impl<switchboard>()}
        , pp{pb->lookup_impl<pose_prediction>()}
        , xwin{pb->lookup_impl<xlib_gl_extended_window>()}
        , _m_clock{pb->lookup_impl<RelativeClock>()}
        , _m_eyebuffer{sb->get_reader<rendered_frame>("eyebuffer")}
        , _m_hologram{sb->get_writer<hologram_input>("hologram_in")}
        , _m_vsync_estimate{sb->get_writer<switchboard::event_wrapper<time_point>>("vsync_estimate")}
        , _m_offload_data{sb->get_writer<texture_pose>("texture_pose")}
        , timewarp_gpu_logger{record_logger_}
        , mtp_logger{record_logger_}
        // TODO: Use #198 to configure this. Delete getenv_or.
        // This is useful for experiments which seek to evaluate the end-effect of timewarp vs no-timewarp.
        // Timewarp poses a "second channel" by which pose data can correct the video stream,
        // which results in a "multipath" between the pose and the video stream.
        // In production systems, this is certainly a good thing, but it makes the system harder to analyze.
        , disable_warp{ILLIXR::str_to_bool(ILLIXR::getenv_or("ILLIXR_TIMEWARP_DISABLE", "False"))}
        , enable_offload{ILLIXR::str_to_bool(ILLIXR::getenv_or("ILLIXR_OFFLOAD_ENABLE", "False"))} { }

private:
    const std::shared_ptr<switchboard>             sb;
    const std::shared_ptr<pose_prediction>         pp;
    const std::shared_ptr<xlib_gl_extended_window> xwin;
    const std::shared_ptr<const RelativeClock>     _m_clock;

    // Note: 0.9 works fine without hologram, but we need a larger safety net with hologram enabled
    static constexpr double DELAY_FRACTION = 0.9;

    // Switchboard plug for application eye buffer.
    switchboard::reader<rendered_frame> _m_eyebuffer;

    // Switchboard plug for sending hologram calls
    switchboard::writer<hologram_input> _m_hologram;

    // Switchboard plug for publishing vsync estimates
    switchboard::writer<switchboard::event_wrapper<time_point>> _m_vsync_estimate;

    // Switchboard plug for publishing offloaded data
    switchboard::writer<texture_pose> _m_offload_data;

    record_coalescer timewarp_gpu_logger;
    record_coalescer mtp_logger;

    GLuint timewarpShaderProgram;

    time_point time_last_swap;

    HMD::hmd_info_t hmd_info;

    // Eye sampler array
    GLuint eye_sampler_0;
    GLuint eye_sampler_1;

    // Eye index uniform
    GLuint tw_eye_index_unif;

    // VAOs
    GLuint tw_vao;

    // Position and UV attribute locations
    GLuint distortion_pos_attr;
    GLuint distortion_uv0_attr;
    GLuint distortion_uv1_attr;
    GLuint distortion_uv2_attr;

    // Distortion mesh information
    GLuint num_distortion_vertices;
    GLuint num_distortion_indices;

    // Distortion mesh CPU buffers and GPU VBO handles
    std::vector<HMD::mesh_coord3d_t> distortion_positions;
    GLuint                           distortion_positions_vbo;
    std::vector<GLuint>              distortion_indices;
    GLuint                           distortion_indices_vbo;
    std::vector<HMD::uv_coord_t>     distortion_uv0;
    GLuint                           distortion_uv0_vbo;
    std::vector<HMD::uv_coord_t>     distortion_uv1;
    GLuint                           distortion_uv1_vbo;
    std::vector<HMD::uv_coord_t>     distortion_uv2;
    GLuint                           distortion_uv2_vbo;

    // Handles to the start and end timewarp
    // transform matrices (3x4 uniforms)
    GLuint tw_start_transform_unif;
    GLuint tw_end_transform_unif;
    // Basic perspective projection matrix
    Eigen::Matrix4f basicProjection;

    // Hologram call data
    ullong _hologram_seq{0};

    bool disable_warp;

    bool enable_offload;

    // PBO buffer for reading texture image
    GLuint PBO_buffer;

    duration offload_duration;

    GLubyte* readTextureImage() {
        const unsigned memSize = display_params::width_pixels * display_params::height_pixels * 3;
        GLubyte*       pixels  = new GLubyte[memSize];

        // Start timer
        time_point startGetTexTime = _m_clock->now();

        // Enable PBO buffer
        glBindBuffer(GL_PIXEL_PACK_BUFFER, PBO_buffer);

        // Read texture image to PBO buffer
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_UNSIGNED_BYTE, (GLvoid*) 0);

        // Transfer texture image from GPU to Pinned Memory(CPU)
        GLubyte* ptr = (GLubyte*) glMapNamedBuffer(PBO_buffer, GL_READ_ONLY);

        // Copy texture to CPU memory
        memcpy(pixels, ptr, memSize);

        // Unmap the buffer
        glUnmapBuffer(GL_PIXEL_PACK_BUFFER);

        // Unbind the buffer
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

        // Record the image collection time
        offload_duration = _m_clock->now() - startGetTexTime;

#ifndef NDEBUG
        double time = duration2double<std::milli>(offload_duration);
        std::cout << "Texture image collecting time: " << time << "ms" << std::endl;
#endif

        return pixels;
    }

    void BuildTimewarp(HMD::hmd_info_t& hmdInfo) {
        // Calculate the number of vertices+indices in the distortion mesh.
        num_distortion_vertices = (hmdInfo.eyeTilesHigh + 1) * (hmdInfo.eyeTilesWide + 1);
        num_distortion_indices  = hmdInfo.eyeTilesHigh * hmdInfo.eyeTilesWide * 6;

        // Allocate memory for the elements/indices array.
        distortion_indices.resize(num_distortion_indices);

        // This is just a simple grid/plane index array, nothing fancy.
        // Same for both eye distortions, too!
        for (int y = 0; y < hmdInfo.eyeTilesHigh; y++) {
            for (int x = 0; x < hmdInfo.eyeTilesWide; x++) {
                const int offset = (y * hmdInfo.eyeTilesWide + x) * 6;

                distortion_indices[offset + 0] = (GLuint) ((y + 0) * (hmdInfo.eyeTilesWide + 1) + (x + 0));
                distortion_indices[offset + 1] = (GLuint) ((y + 1) * (hmdInfo.eyeTilesWide + 1) + (x + 0));
                distortion_indices[offset + 2] = (GLuint) ((y + 0) * (hmdInfo.eyeTilesWide + 1) + (x + 1));

                distortion_indices[offset + 3] = (GLuint) ((y + 0) * (hmdInfo.eyeTilesWide + 1) + (x + 1));
                distortion_indices[offset + 4] = (GLuint) ((y + 1) * (hmdInfo.eyeTilesWide + 1) + (x + 0));
                distortion_indices[offset + 5] = (GLuint) ((y + 1) * (hmdInfo.eyeTilesWide + 1) + (x + 1));
            }
        }

        // There are `num_distortion_vertices` distortion coordinates for each color channel (3) of each eye (2).
        // These are NOT the coordinates of the distorted vertices. They are *coefficients* that will be used to
        // offset the UV coordinates of the distortion mesh.
        std::array<std::array<std::vector<HMD::mesh_coord2d_t>, HMD::NUM_COLOR_CHANNELS>, HMD::NUM_EYES> distort_coords;
        for (auto& eye_coords : distort_coords) {
            for (auto& channel_coords : eye_coords) {
                channel_coords.resize(num_distortion_vertices);
            }
        }
        HMD::BuildDistortionMeshes(distort_coords, hmdInfo);

        // Allocate memory for position and UV CPU buffers.
        const std::size_t num_elems_pos_uv = HMD::NUM_EYES * num_distortion_vertices;
        distortion_positions.resize(num_elems_pos_uv);
        distortion_uv0.resize(num_elems_pos_uv);
        distortion_uv1.resize(num_elems_pos_uv);
        distortion_uv2.resize(num_elems_pos_uv);

        for (int eye = 0; eye < HMD::NUM_EYES; eye++) {
            for (int y = 0; y <= hmdInfo.eyeTilesHigh; y++) {
                for (int x = 0; x <= hmdInfo.eyeTilesWide; x++) {
                    const int index = y * (hmdInfo.eyeTilesWide + 1) + x;

                    // Set the physical distortion mesh coordinates. These are rectangular/gridlike, not distorted.
                    // The distortion is handled by the UVs, not the actual mesh coordinates!
                    distortion_positions[eye * num_distortion_vertices + index].x =
                        (-1.0f + eye + ((float) x / hmdInfo.eyeTilesWide));
                    distortion_positions[eye * num_distortion_vertices + index].y =
                        (-1.0f +
                         2.0f * ((hmdInfo.eyeTilesHigh - (float) y) / hmdInfo.eyeTilesHigh) *
                             ((float) (hmdInfo.eyeTilesHigh * hmdInfo.tilePixelsHigh) / hmdInfo.displayPixelsHigh));
                    distortion_positions[eye * num_distortion_vertices + index].z = 0.0f;

                    // Use the previously-calculated distort_coords to set the UVs on the distortion mesh
                    distortion_uv0[eye * num_distortion_vertices + index].u = distort_coords[eye][0][index].x;
                    distortion_uv0[eye * num_distortion_vertices + index].v = distort_coords[eye][0][index].y;
                    distortion_uv1[eye * num_distortion_vertices + index].u = distort_coords[eye][1][index].x;
                    distortion_uv1[eye * num_distortion_vertices + index].v = distort_coords[eye][1][index].y;
                    distortion_uv2[eye * num_distortion_vertices + index].u = distort_coords[eye][2][index].x;
                    distortion_uv2[eye * num_distortion_vertices + index].v = distort_coords[eye][2][index].y;
                }
            }
        }

        // Construct perspective projection matrix
        math_util::projection_fov(&basicProjection, display_params::fov_x / 2.0f, display_params::fov_x / 2.0f,
                                  display_params::fov_y / 2.0f, display_params::fov_y / 2.0f, rendering_params::near_z,
                                  rendering_params::far_z);
    }

    /* Calculate timewarm transform from projection matrix, view matrix, etc */
    void CalculateTimeWarpTransform(Eigen::Matrix4f& transform, const Eigen::Matrix4f& renderProjectionMatrix,
                                    const Eigen::Matrix4f& renderViewMatrix, const Eigen::Matrix4f& newViewMatrix) {
        // Eigen stores matrices internally in column-major order.
        // However, the (i,j) accessors are row-major (i.e, the first argument
        // is which row, and the second argument is which column.)
        Eigen::Matrix4f texCoordProjection;
        texCoordProjection << 0.5f * renderProjectionMatrix(0, 0), 0.0f, 0.5f * renderProjectionMatrix(0, 2) - 0.5f, 0.0f, 0.0f,
            0.5f * renderProjectionMatrix(1, 1), 0.5f * renderProjectionMatrix(1, 2) - 0.5f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f;

        // Calculate the delta between the view matrix used for rendering and
        // a more recent or predicted view matrix based on new sensor input.
        Eigen::Matrix4f inverseRenderViewMatrix = renderViewMatrix.inverse();

        Eigen::Matrix4f deltaViewMatrix = inverseRenderViewMatrix * newViewMatrix;

        deltaViewMatrix(0, 3) = 0.0f;
        deltaViewMatrix(1, 3) = 0.0f;
        deltaViewMatrix(2, 3) = 0.0f;

        // Accumulate the transforms.
        transform = texCoordProjection * deltaViewMatrix;
    }

    // Get the estimated time of the next swap/next Vsync.
    // This is an estimate, used to wait until *just* before vsync.
    time_point GetNextSwapTimeEstimate() {
        return time_last_swap + display_params::period;
    }

    // Get the estimated amount of time to put the CPU thread to sleep,
    // given a specified percentage of the total Vsync period to delay.
    duration EstimateTimeToSleep(double framePercentage) {
        return std::chrono::duration_cast<duration>((GetNextSwapTimeEstimate() - _m_clock->now()) * framePercentage);
    }

public:
    virtual skip_option _p_should_skip() override {
        using namespace std::chrono_literals;
        // Sleep for approximately 90% of the time until the next vsync.
        // Scheduling granularity can't be assumed to be super accurate here,
        // so don't push your luck (i.e. don't wait too long....) Tradeoff with
        // MTP here. More you wait, closer to the display sync you sample the pose.

        std::this_thread::sleep_for(EstimateTimeToSleep(DELAY_FRACTION));
        if (_m_eyebuffer.get_ro_nullable() != nullptr) {
            return skip_option::run;
        } else {
            // Null means system is nothing has been pushed yet
            // because not all components are initialized yet
            return skip_option::skip_and_yield;
        }
    }

    virtual void _p_thread_setup() override {
        // Wait a vsync for gldemo to go first.
        // This first time_last_swap will be "out of phase" with actual vsync.
        // The second one should be on the dot, since we don't exit the first until actual vsync.
        time_last_swap = _m_clock->now() + display_params::period;

        // Generate reference HMD and physical body dimensions
        HMD::GetDefaultHmdInfo(display_params::width_pixels, display_params::height_pixels, display_params::width_meters,
                               display_params::height_meters, display_params::lens_separation,
                               display_params::meters_per_tan_angle, display_params::aberration, hmd_info);

        // Construct timewarp meshes and other data
        BuildTimewarp(hmd_info);

        // includes setting swap interval
        [[maybe_unused]] const bool gl_result_0 = static_cast<bool>(glXMakeCurrent(xwin->dpy, xwin->win, xwin->glc));
        assert(gl_result_0 && "glXMakeCurrent should not fail");

        // set swap interval for 1
        glXSwapIntervalEXTProc glXSwapIntervalEXT = 0;
        glXSwapIntervalEXT = (glXSwapIntervalEXTProc) glXGetProcAddressARB((const GLubyte*) "glXSwapIntervalEXT");
        glXSwapIntervalEXT(xwin->dpy, xwin->win, 1);

        // Init and verify GLEW
        glewExperimental      = GL_TRUE;
        const GLenum glew_err = glewInit();
        if (glew_err != GLEW_OK) {
            std::cerr << "[timewarp_gl] GLEW Error: " << glewGetErrorString(glew_err) << std::endl;
            ILLIXR::abort("[timewarp_gl] Failed to initialize GLEW");
        }

        glEnable(GL_DEBUG_OUTPUT);

        glDebugMessageCallback(MessageCallback, 0);

        // Create and bind global VAO object
        glGenVertexArrays(1, &tw_vao);
        glBindVertexArray(tw_vao);

#ifdef USE_ALT_EYE_FORMAT
        timewarpShaderProgram =
            init_and_link(timeWarpChromaticVertexProgramGLSL, timeWarpChromaticFragmentProgramGLSL_Alternative);
#else
        timewarpShaderProgram = init_and_link(timeWarpChromaticVertexProgramGLSL, timeWarpChromaticFragmentProgramGLSL);
#endif
        // Acquire attribute and uniform locations from the compiled and linked shader program

        distortion_pos_attr = glGetAttribLocation(timewarpShaderProgram, "vertexPosition");
        distortion_uv0_attr = glGetAttribLocation(timewarpShaderProgram, "vertexUv0");
        distortion_uv1_attr = glGetAttribLocation(timewarpShaderProgram, "vertexUv1");
        distortion_uv2_attr = glGetAttribLocation(timewarpShaderProgram, "vertexUv2");

        tw_start_transform_unif = glGetUniformLocation(timewarpShaderProgram, "TimeWarpStartTransform");
        tw_end_transform_unif   = glGetUniformLocation(timewarpShaderProgram, "TimeWarpEndTransform");
        tw_eye_index_unif       = glGetUniformLocation(timewarpShaderProgram, "ArrayLayer");

        eye_sampler_0 = glGetUniformLocation(timewarpShaderProgram, "Texture[0]");
        eye_sampler_1 = glGetUniformLocation(timewarpShaderProgram, "Texture[1]");

        // Config distortion mesh position vbo
        glGenBuffers(1, &distortion_positions_vbo);
        glBindBuffer(GL_ARRAY_BUFFER, distortion_positions_vbo);

        const std::size_t num_elems_pos_uv = HMD::NUM_EYES * num_distortion_vertices;

        HMD::mesh_coord3d_t* const distortion_positions_data = distortion_positions.data();
        assert(distortion_positions_data != nullptr && "Timewarp allocation should not fail");
        glBufferData(GL_ARRAY_BUFFER, num_elems_pos_uv * sizeof(HMD::mesh_coord3d_t), distortion_positions_data,
                     GL_STATIC_DRAW);

        glVertexAttribPointer(distortion_pos_attr, 3, GL_FLOAT, GL_FALSE, 0, 0);
        // glEnableVertexAttribArray(distortion_pos_attr);

        // Config distortion uv0 vbo
        glGenBuffers(1, &distortion_uv0_vbo);
        glBindBuffer(GL_ARRAY_BUFFER, distortion_uv0_vbo);

        HMD::uv_coord_t* const distortion_uv0_data = distortion_uv0.data();
        assert(distortion_uv0_data != nullptr && "Timewarp allocation should not fail");
        glBufferData(GL_ARRAY_BUFFER, num_elems_pos_uv * sizeof(HMD::uv_coord_t), distortion_uv0_data, GL_STATIC_DRAW);

        glVertexAttribPointer(distortion_uv0_attr, 2, GL_FLOAT, GL_FALSE, 0, 0);
        // glEnableVertexAttribArray(distortion_uv0_attr);

        // Config distortion uv1 vbo
        glGenBuffers(1, &distortion_uv1_vbo);
        glBindBuffer(GL_ARRAY_BUFFER, distortion_uv1_vbo);

        HMD::uv_coord_t* const distortion_uv1_data = distortion_uv1.data();
        assert(distortion_uv1_data != nullptr && "Timewarp allocation should not fail");
        glBufferData(GL_ARRAY_BUFFER, num_elems_pos_uv * sizeof(HMD::uv_coord_t), distortion_uv1_data, GL_STATIC_DRAW);

        glVertexAttribPointer(distortion_uv1_attr, 2, GL_FLOAT, GL_FALSE, 0, 0);
        // glEnableVertexAttribArray(distortion_uv1_attr);

        // Config distortion uv2 vbo
        glGenBuffers(1, &distortion_uv2_vbo);
        glBindBuffer(GL_ARRAY_BUFFER, distortion_uv2_vbo);

        HMD::uv_coord_t* const distortion_uv2_data = distortion_uv2.data();
        assert(distortion_uv2_data != nullptr && "Timewarp allocation should not fail");
        glBufferData(GL_ARRAY_BUFFER, num_elems_pos_uv * sizeof(HMD::uv_coord_t), distortion_uv2_data, GL_STATIC_DRAW);

        glVertexAttribPointer(distortion_uv2_attr, 2, GL_FLOAT, GL_FALSE, 0, 0);
        // glEnableVertexAttribArray(distortion_uv2_attr);

        // Config distortion mesh indices vbo
        glGenBuffers(1, &distortion_indices_vbo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, distortion_indices_vbo);

        GLuint* const distortion_indices_data = distortion_indices.data();
        assert(distortion_indices_data != nullptr && "Timewarp allocation should not fail");
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, num_distortion_indices * sizeof(GLuint), distortion_indices_data, GL_STATIC_DRAW);

        if (enable_offload) {
            // Config PBO for texture image collection
            glGenBuffers(1, &PBO_buffer);
            glBindBuffer(GL_PIXEL_PACK_BUFFER, PBO_buffer);
            glBufferData(GL_PIXEL_PACK_BUFFER, display_params::width_pixels * display_params::height_pixels * 3, 0,
                         GL_STREAM_DRAW);
        }

        [[maybe_unused]] const bool gl_result_1 = static_cast<bool>(glXMakeCurrent(xwin->dpy, None, nullptr));
        assert(gl_result_1 && "glXMakeCurrent should not fail");
    }

    virtual void _p_one_iteration() override {
        [[maybe_unused]] const bool gl_result = static_cast<bool>(glXMakeCurrent(xwin->dpy, xwin->win, xwin->glc));
        assert(gl_result && "glXMakeCurrent should not fail");

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, display_params::width_pixels, display_params::height_pixels);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        glDepthFunc(GL_LEQUAL);

        switchboard::ptr<const rendered_frame> most_recent_frame = _m_eyebuffer.get_ro();

        // Use the timewarp program
        glUseProgram(timewarpShaderProgram);

        // Generate "starting" view matrix, from the pose sampled at the time of rendering the frame
        Eigen::Matrix4f viewMatrix   = Eigen::Matrix4f::Identity();
        viewMatrix.block(0, 0, 3, 3) = most_recent_frame->render_pose.pose.orientation.toRotationMatrix();

        // We simulate two asynchronous view matrices, one at the beginning of
        // display refresh, and one at the end of display refresh. The
        // distortion shader will lerp between these two predictive view
        // transformations as it renders across the horizontal view,
        // compensating for display panel refresh delay (wow!)
        Eigen::Matrix4f viewMatrixBegin = Eigen::Matrix4f::Identity();
        Eigen::Matrix4f viewMatrixEnd   = Eigen::Matrix4f::Identity();

        const fast_pose_type latest_pose  = disable_warp ? most_recent_frame->render_pose : pp->get_fast_pose();
        viewMatrixBegin.block(0, 0, 3, 3) = latest_pose.pose.orientation.toRotationMatrix();

        // TODO: We set the "end" pose to the same as the beginning pose, but this really should be the pose for
        // `display_period` later
        viewMatrixEnd = viewMatrixBegin;

        // Calculate the timewarp transformation matrices. These are a product
        // of the last-known-good view matrix and the predictive transforms.
        Eigen::Matrix4f timeWarpStartTransform4x4;
        Eigen::Matrix4f timeWarpEndTransform4x4;

        // Calculate timewarp transforms using predictive view transforms
        CalculateTimeWarpTransform(timeWarpStartTransform4x4, basicProjection, viewMatrix, viewMatrixBegin);
        CalculateTimeWarpTransform(timeWarpEndTransform4x4, basicProjection, viewMatrix, viewMatrixEnd);

        glUniformMatrix4fv(tw_start_transform_unif, 1, GL_FALSE, (GLfloat*) (timeWarpStartTransform4x4.data()));
        glUniformMatrix4fv(tw_end_transform_unif, 1, GL_FALSE, (GLfloat*) (timeWarpEndTransform4x4.data()));

        // Debugging aid, toggle switch for rendering in the fragment shader
        glUniform1i(glGetUniformLocation(timewarpShaderProgram, "ArrayIndex"), 0);
        glUniform1i(eye_sampler_0, 0);

#ifndef USE_ALT_EYE_FORMAT
        // Bind the shared texture handle
        glBindTexture(GL_TEXTURE_2D_ARRAY, most_recent_frame->texture_handle);
#endif

        glBindVertexArray(tw_vao);

        auto gpu_start_wall_time = _m_clock->now();

        GLuint   query;
        GLuint64 elapsed_time = 0;

        glGenQueries(1, &query);
        glBeginQuery(GL_TIME_ELAPSED, query);

        // Loop over each eye.
        for (int eye = 0; eye < HMD::NUM_EYES; eye++) {
#ifdef USE_ALT_EYE_FORMAT // If we're using Monado-style buffers we need to rebind eyebuffers.... eugh!
            glBindTexture(GL_TEXTURE_2D, most_recent_frame->texture_handles[eye]);
#endif

            // The distortion_positions_vbo GPU buffer already contains
            // the distortion mesh for both eyes! They are contiguously
            // laid out in GPU memory. Therefore, on each eye render,
            // we set the attribute pointer to be offset by the full
            // eye's distortion mesh size, rendering the correct eye mesh
            // to that region of the screen. This prevents re-uploading
            // GPU data for each eye.
            glBindBuffer(GL_ARRAY_BUFFER, distortion_positions_vbo);
            glVertexAttribPointer(distortion_pos_attr, 3, GL_FLOAT, GL_FALSE, 0,
                                  (void*) (eye * num_distortion_vertices * sizeof(HMD::mesh_coord3d_t)));
            glEnableVertexAttribArray(distortion_pos_attr);

            // We do the exact same thing for the UV GPU memory.
            glBindBuffer(GL_ARRAY_BUFFER, distortion_uv0_vbo);
            glVertexAttribPointer(distortion_uv0_attr, 2, GL_FLOAT, GL_FALSE, 0,
                                  (void*) (eye * num_distortion_vertices * sizeof(HMD::mesh_coord2d_t)));
            glEnableVertexAttribArray(distortion_uv0_attr);

            // We do the exact same thing for the UV GPU memory.
            glBindBuffer(GL_ARRAY_BUFFER, distortion_uv1_vbo);
            glVertexAttribPointer(distortion_uv1_attr, 2, GL_FLOAT, GL_FALSE, 0,
                                  (void*) (eye * num_distortion_vertices * sizeof(HMD::mesh_coord2d_t)));
            glEnableVertexAttribArray(distortion_uv1_attr);

            // We do the exact same thing for the UV GPU memory.
            glBindBuffer(GL_ARRAY_BUFFER, distortion_uv2_vbo);
            glVertexAttribPointer(distortion_uv2_attr, 2, GL_FLOAT, GL_FALSE, 0,
                                  (void*) (eye * num_distortion_vertices * sizeof(HMD::mesh_coord2d_t)));
            glEnableVertexAttribArray(distortion_uv2_attr);

#ifndef USE_ALT_EYE_FORMAT // If we are using normal ILLIXR-format eyebuffers
            // Specify which layer of the eye texture we're going to be using.
            // Each eye has its own layer.
            glUniform1i(tw_eye_index_unif, eye);
#endif

            // Interestingly, the element index buffer is identical for both eyes, and is
            // reused for both eyes. Therefore glDrawElements can be immediately called,
            // with the UV and position buffers correctly offset.
            glDrawElements(GL_TRIANGLES, num_distortion_indices, GL_UNSIGNED_INT, (void*) 0);
        }

        glFinish();
        glEndQuery(GL_TIME_ELAPSED);

#ifndef NDEBUG
        const duration time_since_render = _m_clock->now() - most_recent_frame->render_time;

        if (log_count > LOG_PERIOD) {
            const double time_since_render_ms_d = duration2double<std::milli>(time_since_render);
            std::cout << "\033[1;36m[TIMEWARP]\033[0m Time since render: " << time_since_render_ms_d << "ms" << std::endl;
        }

        if (time_since_render > display_params::period) {
            std::cout << "\033[0;31m[TIMEWARP: CRITICAL]\033[0m Stale frame!" << std::endl;
        }
#endif
        // Call Hologram
        _m_hologram.put(_m_hologram.allocate<hologram_input>(++_hologram_seq));

        // Call swap buffers; when vsync is enabled, this will return to the
        // CPU thread once the buffers have been successfully swapped.
        [[maybe_unused]] time_point time_before_swap = _m_clock->now();

        glXSwapBuffers(xwin->dpy, xwin->win);

        // The swap time needs to be obtained and published as soon as possible
        time_last_swap                              = _m_clock->now();
        [[maybe_unused]] time_point time_after_swap = time_last_swap;

        // Now that we have the most recent swap time, we can publish the new estimate.
        _m_vsync_estimate.put(_m_vsync_estimate.allocate<switchboard::event_wrapper<time_point>>(GetNextSwapTimeEstimate()));

        std::chrono::nanoseconds imu_to_display     = time_last_swap - latest_pose.pose.sensor_time;
        std::chrono::nanoseconds predict_to_display = time_last_swap - latest_pose.predict_computed_time;
        std::chrono::nanoseconds render_to_display  = time_last_swap - most_recent_frame->render_time;

        mtp_logger.log(record{mtp_record,
                              {
                                  {iteration_no},
                                  {time_last_swap},
                                  {imu_to_display},
                                  {predict_to_display},
                                  {render_to_display},
                              }});

        if (enable_offload) {
            // Read texture image from texture buffer
            GLubyte* image = readTextureImage();

            // Publish image and pose
            _m_offload_data.put(_m_offload_data.allocate<texture_pose>(
                texture_pose{offload_duration, image, time_last_swap, latest_pose.pose.position, latest_pose.pose.orientation,
                             most_recent_frame->render_pose.pose.orientation}));
        }

#ifndef NDEBUG
        if (log_count > LOG_PERIOD) {
            const double     time_swap         = duration2double<std::milli>(time_after_swap - time_before_swap);
            const double     latency_mtd       = duration2double<std::milli>(imu_to_display);
            const double     latency_ptd       = duration2double<std::milli>(predict_to_display);
            const double     latency_rtd       = duration2double<std::milli>(render_to_display);
            const time_point time_next_swap    = GetNextSwapTimeEstimate();
            const double     timewarp_estimate = duration2double<std::milli>(time_next_swap - time_last_swap);

            std::cout << "\033[1;36m[TIMEWARP]\033[0m Swap time: " << time_swap << "ms" << std::endl
                      << "\033[1;36m[TIMEWARP]\033[0m Motion-to-display latency: " << latency_mtd << "ms" << std::endl
                      << "\033[1;36m[TIMEWARP]\033[0m Prediction-to-display latency: " << latency_ptd << "ms" << std::endl
                      << "\033[1;36m[TIMEWARP]\033[0m Render-to-display latency: " << latency_rtd << "ms" << std::endl
                      << "Next swap in: " << timewarp_estimate << "ms in the future" << std::endl;
        }
#endif

        // retrieving the recorded elapsed time
        // wait until the query result is available
        int done = 0;
        glGetQueryObjectiv(query, GL_QUERY_RESULT_AVAILABLE, &done);

        while (!done) {
            std::this_thread::yield();
            glGetQueryObjectiv(query, GL_QUERY_RESULT_AVAILABLE, &done);
        }

        // get the query result
        glGetQueryObjectui64v(query, GL_QUERY_RESULT, &elapsed_time);

        timewarp_gpu_logger.log(record{timewarp_gpu_record,
                                       {
                                           {iteration_no},
                                           {gpu_start_wall_time},
                                           {_m_clock->now()},
                                           {std::chrono::nanoseconds(elapsed_time)},
                                       }});

#ifndef NDEBUG
        if (log_count > LOG_PERIOD) {
            log_count = 0;
        } else {
            log_count++;
        }
#endif
    }

#ifndef NDEBUG
    size_t log_count  = 0;
    size_t LOG_PERIOD = 20;
#endif
};

PLUGIN_MAIN(timewarp_gl)
