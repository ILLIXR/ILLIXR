// clang-format off
#include <GL/glew.h> // GLEW has to be loaded before other GL libraries
#include <vulkan/vulkan.h>
// clang-format on

#include "common/data_format.hpp"
#include "common/error_util.hpp"
#include "common/extended_window.hpp"
#include "common/global_module_defs.hpp"
#include "common/math_util.hpp"
#include "common/pose_prediction.hpp"
#include "common/shader_util.hpp"
#include "common/switchboard.hpp"
#include "common/threadloop.hpp"
#include "shaders/basic_shader.hpp"
#include "shaders/timewarp_shader.hpp"
#include "utils/hmd.hpp"

#include <atomic>
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
        , enable_offload{ILLIXR::str_to_bool(ILLIXR::getenv_or("ILLIXR_OFFLOAD_ENABLE", "False"))} {
#ifndef ILLIXR_MONADO
        const std::shared_ptr<xlib_gl_extended_window> xwin = pb->lookup_impl<xlib_gl_extended_window>();
        dpy                                                 = xwin->dpy;
        root                                                = xwin->win;
        glc                                                 = xwin->glc;
#else
        // If we use Monado, timewarp_gl must create its own GL context because the extended window isn't used
        std::cout << "Timewarp creating GL Context" << std::endl;
        GLint        attr[] = {GLX_RGBA, GLX_DEPTH_SIZE, 24, GLX_DOUBLEBUFFER, None};
        XVisualInfo* vi;
        /* open display */
        if (!(dpy = XOpenDisplay(NULL))) {
            fprintf(stderr, "cannot connect to X server\n\n");
            exit(1);
        }

        /* get root window */
        root = DefaultRootWindow(dpy);

        /* get visual matching attr */
        if (!(vi = glXChooseVisual(dpy, 0, attr))) {
            fprintf(stderr, "no appropriate visual found\n\n");
            exit(1);
        }

        /* create a context using the root window */
        if (!(glc = glXCreateContext(dpy, vi, NULL, GL_TRUE))) {
            fprintf(stderr, "failed to create context\n\n");
            exit(1);
        }
#endif
        client_backend      = graphics_api::TBD;
        rendering_ready     = false;
        image_handles_ready = false;
#ifdef ILLIXR_MONADO
        semaphore_handles_ready = false;
#else
        semaphore_handles_ready = true;
#endif

        sb->schedule<image_handle>(id, "image_handle", [this](switchboard::ptr<const image_handle> handle, std::size_t) {
        // only 2 swapchains (for the left and right eye) are supported for now.
#ifdef ILLIXR_MONADO
            static bool left_output_ready = false, right_output_ready = false;
#else
            static bool left_output_ready = true, right_output_ready = true;
#endif

            switch (handle->usage) {
            case swapchain_usage::LEFT_SWAPCHAIN: {
                this->_m_eye_image_handles[0].push_back(*handle);
                this->_m_eye_swapchains_size[0] = handle->num_images;
                break;
            }
            case swapchain_usage::RIGHT_SWAPCHAIN: {
                this->_m_eye_image_handles[1].push_back(*handle);
                this->_m_eye_swapchains_size[1] = handle->num_images;
                break;
            }
#ifdef ILLIXR_MONADO
            case swapchain_usage::LEFT_RENDER: {
                this->_m_eye_output_handles[0] = *handle;
                left_output_ready              = true;
                break;
            }
            case swapchain_usage::RIGHT_RENDER: {
                this->_m_eye_output_handles[1] = *handle;
                right_output_ready             = true;
                break;
            }
#endif
            default: {
                std::cout << "Invalid swapchain usage provided" << std::endl;
                break;
            }
            }

            if (client_backend == graphics_api::TBD) {
                client_backend = handle->type;
            } else {
                assert(client_backend == handle->type);
            }

            if (this->_m_eye_image_handles[0].size() == this->_m_eye_swapchains_size[0] &&
                this->_m_eye_image_handles[1].size() == this->_m_eye_swapchains_size[1] && left_output_ready &&
                right_output_ready) {
                this->image_handles_ready = true;
            }
        });

#ifdef ILLIXR_MONADO
        sb->schedule<semaphore_handle>(id, "semaphore_handle",
                                       [this](switchboard::ptr<const semaphore_handle> handle, std::size_t) {
                                           // We need one semaphore to indicate when the reprojection is ready, and another when
                                           // it's done
                                           static bool left_lsr_ready = false, right_lsr_ready = false;
                                           static bool left_lsr_complete = false, right_lsr_complete = false;
                                           switch (handle->usage) {
                                           case semaphore_usage::LEFT_LSR_READY: {
                                               _m_semaphore_handles[0][0] = *handle;
                                               left_lsr_ready             = true;
                                               break;
                                           }
                                           case semaphore_usage::RIGHT_LSR_READY: {
                                               _m_semaphore_handles[1][0] = *handle;
                                               right_lsr_ready            = true;
                                               break;
                                           }
                                           case semaphore_usage::LEFT_LSR_COMPLETE: {
                                               _m_semaphore_handles[0][1] = *handle;
                                               left_lsr_complete          = true;
                                               break;
                                           }
                                           case semaphore_usage::RIGHT_LSR_COMPLETE: {
                                               _m_semaphore_handles[1][1] = *handle;
                                               right_lsr_complete         = true;
                                               break;
                                           }
                                           default: {
                                               std::cout << "Invalid semaphore usage provided" << std::endl;
                                               break;
                                           }
                                           }

                                           if (left_lsr_ready && right_lsr_ready && left_lsr_complete && right_lsr_complete) {
                                               this->semaphore_handles_ready = true;
                                           }
                                       });
#endif
    }

private:
    const std::shared_ptr<switchboard>         sb;
    const std::shared_ptr<pose_prediction>     pp;
    const std::shared_ptr<const RelativeClock> _m_clock;

    // OpenGL objects
    Display*   dpy;
    Window     root;
    GLXContext glc;

    // Note: 0.9 works fine without hologram, but we need a larger safety net with hologram enabled
    static constexpr double DELAY_FRACTION = 0.9;

    // Shared objects between ILLIXR and the application (either gldemo or Monado)
    bool              rendering_ready;
    graphics_api      client_backend;
    std::atomic<bool> image_handles_ready;
    std::atomic<bool> semaphore_handles_ready;

    // Left and right eye images
    std::array<std::vector<image_handle>, 2> _m_eye_image_handles;
    std::array<std::vector<GLuint>, 2>       _m_eye_swapchains;
    std::array<size_t, 2>                    _m_eye_swapchains_size;

    // Intermediate timewarp framebuffers for left and right eye textures
    std::array<GLuint, 2> _m_eye_output_textures;
    std::array<GLuint, 2> _m_eye_framebuffers;

#ifdef ILLIXR_MONADO
    std::array<image_handle, 2> _m_eye_output_handles;

    // Semaphores to synchronize between Monado and ILLIXR
    // Left and right; ready and complete.
    std::array<std::array<semaphore_handle, 2>, 2> _m_semaphore_handles;
    std::array<std::array<GLuint, 2>, 2>           _m_semaphores;
#endif

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
    // bool uniform to check if the Y axis needs to be inverted
    GLuint flip_y_unif;
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

    GLuint ConvertVkFormatToGL(int64_t vk_format) {
        switch (vk_format) {
        case VK_FORMAT_R8G8B8A8_UNORM:
            return GL_RGBA8;
        case VK_FORMAT_B8G8R8A8_SRGB:
        case VK_FORMAT_R8G8B8A8_SRGB:
            return GL_SRGB8_ALPHA8;
        default:
            return 0;
        }
    }

    void ImportVulkanImage(const vk_image_handle& vk_handle, swapchain_usage usage) {
        [[maybe_unused]] const bool gl_result = static_cast<bool>(glXMakeCurrent(dpy, root, glc));
        assert(gl_result && "glXMakeCurrent should not fail");
        assert(GLEW_EXT_memory_object_fd && "[timewarp_gl] Missing object memory extensions for Vulkan-GL interop");

        // first get the memory handle of the vulkan object
        GLuint memory_handle;
        GLint  dedicated = GL_TRUE;
        glCreateMemoryObjectsEXT(1, &memory_handle);
        glMemoryObjectParameterivEXT(memory_handle, GL_DEDICATED_MEMORY_OBJECT_EXT, &dedicated);
        glImportMemoryFdEXT(memory_handle, vk_handle.allocation_size, GL_HANDLE_TYPE_OPAQUE_FD_EXT, vk_handle.file_descriptor);

        // then use the imported memory as the opengl texture.
        // since we're writing to an intermediate texture that's the same memory format as Monado's layer renderer,
        // there's no need to reformat anything.
        GLuint format = ConvertVkFormatToGL(vk_handle.format);
        assert(format != 0 && "Given Vulkan format not handled!");
        GLuint image_handle;
        glGenTextures(1, &image_handle);
        glBindTexture(GL_TEXTURE_2D, image_handle);
        glTextureStorageMem2DEXT(image_handle, 1, format, vk_handle.width, vk_handle.height, memory_handle, 0);

        switch (usage) {
        case swapchain_usage::LEFT_SWAPCHAIN: {
            _m_eye_swapchains[0].push_back(image_handle);
            break;
        }
        case swapchain_usage::RIGHT_SWAPCHAIN: {
            _m_eye_swapchains[1].push_back(image_handle);
            break;
        }
        case swapchain_usage::LEFT_RENDER: {
            _m_eye_output_textures[0] = image_handle;
            break;
        }
        case swapchain_usage::RIGHT_RENDER: {
            _m_eye_output_textures[1] = image_handle;
            break;
        }
        default: {
            assert(false && "Invalid swapchain usage");
            break;
        }
        }
    }

#ifdef ILLIXR_MONADO
    void ImportVulkanSemaphore(const semaphore_handle& vk_handle) {
        [[maybe_unused]] const bool gl_result = static_cast<bool>(glXMakeCurrent(dpy, root, glc));
        assert(gl_result && "glXMakeCurrent should not fail");
        assert(GLEW_EXT_memory_object_fd && "[timewarp_gl] Missing object memory extensions for Vulkan-GL interop");

        GLuint semaphore_handle;
        glGenSemaphoresEXT(1, &semaphore_handle);
        glImportSemaphoreFdEXT(semaphore_handle, GL_HANDLE_TYPE_OPAQUE_FD_EXT, vk_handle.vk_handle);

        switch (vk_handle.usage) {
        case semaphore_usage::LEFT_LSR_READY: {
            _m_semaphores[0][0] = semaphore_handle;
            std::cout << "LEFT LSR READY HANDLE" << std::endl;
            break;
        }
        case semaphore_usage::RIGHT_LSR_READY: {
            _m_semaphores[1][0] = semaphore_handle;
            std::cout << "RIGHT LSR READY HANDLE" << std::endl;
            break;
        }
        case semaphore_usage::LEFT_LSR_COMPLETE: {
            _m_semaphores[0][1] = semaphore_handle;
            std::cout << "LEFT LSR COMPLETE HANDLE" << std::endl;
            break;
        }
        case semaphore_usage::RIGHT_LSR_COMPLETE: {
            _m_semaphores[1][1] = semaphore_handle;
            std::cout << "RIGHT LSR COMPLETE HANDLE" << std::endl;
            break;
        }
        default: {
            assert(false && "Invalid semaphore usage");
            break;
        }
        }
    }
#endif

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
                        (-1.0f + 2.0f * ((float) x / hmdInfo.eyeTilesWide));
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
        if (image_handles_ready.load() && semaphore_handles_ready.load() && _m_eyebuffer.get_ro_nullable() != nullptr) {
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
        [[maybe_unused]] const bool gl_result_0 = static_cast<bool>(glXMakeCurrent(dpy, root, glc));
        assert(gl_result_0 && "glXMakeCurrent should not fail");

        // set swap interval for 1
        glXSwapIntervalEXTProc glXSwapIntervalEXT = 0;
        glXSwapIntervalEXT = (glXSwapIntervalEXTProc) glXGetProcAddressARB((const GLubyte*) "glXSwapIntervalEXT");
        glXSwapIntervalEXT(dpy, root, 1);

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
        timewarpShaderProgram   = init_and_link(timeWarpChromaticVertexProgramGLSL, timeWarpChromaticFragmentProgramGLSL);
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

        flip_y_unif = glGetUniformLocation(timewarpShaderProgram, "flipY");

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

        [[maybe_unused]] const bool gl_result_1 = static_cast<bool>(glXMakeCurrent(dpy, None, nullptr));
        assert(gl_result_1 && "glXMakeCurrent should not fail");
    }

    virtual void _p_one_iteration() override {
        [[maybe_unused]] const bool gl_result = static_cast<bool>(glXMakeCurrent(dpy, root, glc));
        assert(gl_result && "glXMakeCurrent should not fail");

        if (!rendering_ready) {
            assert(image_handles_ready);

            for (int eye = 0; eye < 2; eye++) {
                uint32_t num_images = _m_eye_image_handles[eye][0].num_images;
                for (uint32_t image_index = 0; image_index < num_images; image_index++) {
                    image_handle image = _m_eye_image_handles[eye][image_index];
                    if (client_backend == graphics_api::OPENGL) {
                        _m_eye_swapchains[eye].push_back(image.gl_handle);
                    } else {
                        ImportVulkanImage(image.vk_handle, image.usage);
                    }
                }
            }

            // If we're using Monado, we need to import the eye output textures to render to.
            // Otherwise with native, we can directly create the textures.
            for (int eye = 0; eye < 2; eye++) {
#ifdef ILLIXR_MONADO
                image_handle image = _m_eye_output_handles[eye];
                ImportVulkanImage(image.vk_handle, image.usage);

                // Each eye also has an associated semaphore for ready and complete
                for (int usage = 0; usage < 2; usage++) {
                    ImportVulkanSemaphore(_m_semaphore_handles[eye][usage]);
                }
#else
                GLuint eye_output_texture;
                glGenTextures(1, &eye_output_texture);
                _m_eye_output_textures[eye] = eye_output_texture;

                glBindTexture(GL_TEXTURE_2D, eye_output_texture);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, display_params::width_pixels * 0.5f, display_params::height_pixels, 0,
                               GL_RGB, GL_FLOAT, NULL);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
#endif

                // Once the eye output textures are created, we bind them to the framebuffer
                GLuint framebuffer;
                glGenFramebuffers(1, &framebuffer);
                _m_eye_framebuffers[eye] = framebuffer;

                glBindFramebuffer(GL_FRAMEBUFFER, _m_eye_framebuffers[eye]);
                glBindTexture(GL_TEXTURE_2D, _m_eye_output_textures[eye]);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _m_eye_output_textures[eye], 0);

                uint32_t attachment = GL_COLOR_ATTACHMENT0;
                glDrawBuffers(1, &attachment);
            }

            rendering_ready = true;
        }

        switchboard::ptr<const rendered_frame> most_recent_frame = _m_eyebuffer.get_ro();
        std::cout << "TIMEWARP STARTING" << std::endl;

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

        // Flip the Y axis if the client is using a Vulkan backend
        glUniform1i(flip_y_unif, false);

        // Debugging aid, toggle switch for rendering in the fragment shader
        glUniform1i(glGetUniformLocation(timewarpShaderProgram, "ArrayIndex"), 0);
        glUniform1i(eye_sampler_0, 0);

#ifndef USE_ALT_EYE_FORMAT
        // Bind the shared texture handle
        glBindTexture(GL_TEXTURE_2D_ARRAY, most_recent_frame->texture_handle);
#endif

        glBindVertexArray(tw_vao);

        auto gpu_start_wall_time = _m_clock->now();

        GLuint   query        = 0;
        GLuint64 elapsed_time = 0;

        glGenQueries(1, &query);
        glBeginQuery(GL_TIME_ELAPSED, query);

        // Loop over each eye.
        for (int eye = 0; eye < HMD::NUM_EYES; eye++) {
            // Choose the appropriate texture to render to
#ifdef ILLIXR_MONADO
            // GLenum src_layout = GL_LAYOUT_COLOR_ATTACHMENT_EXT;
            // glWaitSemaphoreEXT(_m_semaphores[eye][0], 0, nullptr, 1,
            // &_m_eye_swapchains[eye][most_recent_frame->swapchain_indices[eye]], &src_layout);
#endif
            glBindFramebuffer(GL_FRAMEBUFFER, _m_eye_framebuffers[eye]);
            glViewport(0, 0, display_params::width_pixels * 0.5, display_params::height_pixels);
            glClearColor(1.0, 1.0, 1.0, 1.0);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
            glDepthFunc(GL_LEQUAL);

#ifdef USE_ALT_EYE_FORMAT // If we're using Monado-style buffers we need to rebind eyebuffers.... eugh!
            [[maybe_unused]] const bool isTexture =
                static_cast<bool>(glIsTexture(_m_eye_swapchains[eye][most_recent_frame->swapchain_indices[eye]]));
            assert(isTexture && "The requested image is not a texture!");
            glBindTexture(GL_TEXTURE_2D, _m_eye_swapchains[eye][most_recent_frame->swapchain_indices[eye]]);
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

#ifdef ILLIXR_MONADO
            GLenum dst_layout = GL_LAYOUT_SHADER_READ_ONLY_EXT;
            glSignalSemaphoreEXT(_m_semaphores[eye][1], 0, nullptr, 1, &_m_eye_output_textures[eye], &dst_layout);
#endif
        }

        glFinish();
        glEndQuery(GL_TIME_ELAPSED);

        std::cout << "TIMEWARP COMPLETE" << std::endl;

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

        // If we're not using Monado, we want to composite the left and right buffers into one
#ifndef ILLIXR_MONADO
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, display_params::width_pixels, display_params::height_pixels);

        // Blit the left and right color buffers onto the default color buffer
        glBindFramebuffer(GL_READ_FRAMEBUFFER, _m_eye_framebuffers[0]);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, display_params::width_pixels * 0.5, display_params::height_pixels, 0, 0,
                          display_params::width_pixels * 0.5, display_params::height_pixels, GL_COLOR_BUFFER_BIT, GL_NEAREST);

        glBindFramebuffer(GL_READ_FRAMEBUFFER, _m_eye_framebuffers[1]);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, display_params::width_pixels * 0.5, display_params::height_pixels,
                          display_params::width_pixels * 0.5, 0, display_params::width_pixels, display_params::height_pixels,
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);

        glXSwapBuffers(dpy, root);
#endif

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
