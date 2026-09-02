#pragma once

#define GL_GLEXT_PROTOTYPES
#define XR_USE_PLATFORM_XLIB
#define XR_USE_GRAPHICS_API_OPENGL
#define GLFW_EXPOSE_NATIVE_X11
#define GLFW_EXPOSE_NATIVE_GLX

#include "illixr/data_format/openxr_view_frame.hpp"
#include "illixr/data_format/quest_controller.hpp"
#include "illixr/data_format/stereo_frame.hpp"
#include "illixr/plugin.hpp"
#include "illixr/relative_clock.hpp"
#include "illixr/stoplight.hpp"
#include "illixr/switchboard.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <GL/gl.h>
#include <GL/glx.h>
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <openxr.h>
#include <openxr_platform.h>
#include <string>
#include <thread>
#include <vector>
#include <X11/Xlib.h>

namespace ILLIXR {

/**
 * Own a desktop OpenXR session for the ALVR/SteamVR compatibility profile.
 *
 * Each OpenXR frame produces sequence-matched `quest_controller` and
 * `openxr_view` snapshots for Boba. In the opposite direction, the plugin
 * consumes mmap-backed `stereo_frame` output and submits it to the runtime,
 * including Boba's vector and modal overlays. The native Quest profile uses
 * the Quest-side `openxr_interface` instead, but both paths share the same data
 * contracts.
 */
class openxr_quest_controller final : public plugin {
public:
    /** Bind switchboard endpoints and read logging configuration. */
    openxr_quest_controller(const std::string& name, phonebook* pb);
    /** Stop the worker before destroying OpenXR/OpenGL member state. */
    ~openxr_quest_controller() override;

    /** Create the graphics binding and start the OpenXR worker. */
    void start() override;

    /** Stop the session thread and destroy all OpenXR/OpenGL resources. */
    void stop() override;

private:
    using controller_input   = data_format::quest_controller_input;
    using hand_controller    = data_format::quest_hand_controller;
    using controller_pose    = data_format::quest_controller_pose;
    using controller_button  = data_format::quest_controller_button;
    using controller_axis2d  = data_format::quest_controller_axis2d;
    using controller_profile = data_format::quest_controller_profile;
    using view_frame         = data_format::openxr_view_frame;
    using eye_view           = data_format::openxr_eye_view;
    using stereo_frame       = data_format::stereo_frame;

    /** Read-only RAII mapping for Boba's recyclable output rings. */
    struct mapped_file {
        int                 fd{-1};
        const std::uint8_t* data{nullptr};
        std::size_t         size{0};
        std::string         path;

        mapped_file()                              = default;
        mapped_file(const mapped_file&)            = delete;
        mapped_file& operator=(const mapped_file&) = delete;
        ~mapped_file();

        /** Unmap the region, close its descriptor, and clear the remembered path. */
        void reset();
    };

    /** One per-eye OpenXR swapchain and its runtime-owned OpenGL images. */
    struct swapchain_view {
        XrSwapchain                            handle{XR_NULL_HANDLE};
        std::uint32_t                          width{0};
        std::uint32_t                          height{0};
        std::vector<XrSwapchainImageOpenGLKHR> images;
    };

    // Hidden desktop OpenGL context used to create the OpenXR GLX binding.
    void initialize_graphics();
    void destroy_graphics();
    bool resolve_glx_binding();

    // OpenXR lifecycle and per-frame event/render loop.
    void run();
    bool initialize_openxr();
    void destroy_openxr();
    bool pump_events();
    bool process_frame();

    /** Query the runtime's two-eye configuration and recommended dimensions. */
    bool enumerate_view_configuration();

    /** Locate both eyes at the same predicted display time used for controller poses. */
    bool query_views(XrTime sample_time, view_frame* views);

    // Swapchain, shared-ring upload, and compositor submission.
    bool create_swapchains();
    void destroy_swapchains();
    bool initialize_stereo_renderer();
    void destroy_stereo_renderer();
    /** Atomically snapshot the newest frame/overlay/modal generations into GL textures. */
    bool update_stereo_frame();
    bool ensure_mapped(const std::string& path, mapped_file* mapping);
    bool ensure_source_textures(std::uint32_t width, std::uint32_t height);
    bool render_projection_layer(XrCompositionLayerProjection*                    layer,
                                 std::array<XrCompositionLayerProjectionView, 2>* projection_views);
    bool render_eye(std::size_t eye_index, GLuint swapchain_image);

    // Controller action creation and cross-runtime binding suggestions.
    bool create_actions();
    bool suggest_bindings();
    bool create_action(XrActionType type, const char* name, const char* localized_name, XrAction* action);
    bool create_action_space(XrAction action, XrPath hand_path, XrSpace* space, const char* label);
    bool add_binding(std::vector<XrActionSuggestedBinding>* bindings, XrAction action, const char* path_string);
    bool suggest_profile_bindings(const char* profile_string, const std::vector<XrActionSuggestedBinding>& bindings);

    // Per-frame action-state queries. Inactive sources remain explicit neutral values.
    bool query_hand(std::size_t hand_index, XrTime sample_time, hand_controller* hand);
    bool query_pose(XrAction action, XrSpace action_space, XrPath hand_path, XrTime sample_time, controller_pose* pose);
    bool query_boolean(XrAction action, XrPath hand_path, controller_button* button);
    bool query_float(XrAction action, XrPath hand_path, float pressed_threshold, controller_button* button);
    bool query_axis2d(XrAction action, XrPath hand_path, controller_axis2d* axis);
    bool refresh_interaction_profiles();

    // Optional transition-only input diagnostics; no per-frame pose spam.
    void log_input_changes(const controller_input& input);
    void log_hand_changes(const char* hand_name, bool left, const hand_controller& current, const hand_controller& previous,
                          bool first_sample);
    void log_button_change(const char* hand_name, const char* button_name, const controller_button& current,
                           const controller_button& previous, bool first_sample);

    // OpenXR error/path presentation helpers.
    bool        check_xr(XrResult result, const char* operation) const;
    std::string xr_result_string(XrResult result) const;
    std::string path_string(XrPath path) const;

    // Stable mappings between OpenXR identifiers and switchboard-facing enums/log labels.
    static controller_profile profile_from_path(const std::string& path);
    static const char*        profile_label(controller_profile profile);
    static const char*        session_state_label(XrSessionState state);
    static void               merge_button(controller_button* destination, const controller_button& source);

    // ILLIXR services and typed topics.
    const std::shared_ptr<switchboard>          switchboard_;
    const std::shared_ptr<const relative_clock> clock_;
    const std::shared_ptr<const stoplight>      stoplight_;
    switchboard::writer<controller_input>       controller_writer_;
    switchboard::writer<view_frame>             view_writer_;
    switchboard::reader<stereo_frame>           stereo_reader_;

    // Worker control.
    bool              log_input_{true};
    std::atomic<bool> stop_requested_{false};
    std::thread       worker_;

    // Hidden GLFW/X11/GLX objects supplied in XrGraphicsBindingOpenGLXlibKHR.
    bool          glfw_initialized_{false};
    GLFWwindow*   window_{nullptr};
    Display*      x_display_{nullptr};
    GLXFBConfig   glx_fb_config_{nullptr};
    GLXDrawable   glx_drawable_{0};
    GLXContext    glx_context_{nullptr};
    std::uint32_t visual_id_{0};

    // Core OpenXR instance, HMD system, session, and LOCAL reference space.
    XrInstance instance_{XR_NULL_HANDLE};
    XrSystemId system_id_{XR_NULL_SYSTEM_ID};
    XrSession  session_{XR_NULL_HANDLE};
    XrSpace    local_space_{XR_NULL_HANDLE};

    // Shared left/right action set. Individual action state is selected by subaction path.
    XrActionSet action_set_{XR_NULL_HANDLE};
    XrAction    grip_pose_action_{XR_NULL_HANDLE};
    XrAction    aim_pose_action_{XR_NULL_HANDLE};
    XrAction    trigger_click_action_{XR_NULL_HANDLE};
    XrAction    trigger_value_action_{XR_NULL_HANDLE};
    XrAction    primary_click_action_{XR_NULL_HANDLE};
    XrAction    secondary_click_action_{XR_NULL_HANDLE};
    XrAction    thumbstick_click_action_{XR_NULL_HANDLE};
    XrAction    thumbstick_axis_action_{XR_NULL_HANDLE};
    XrAction    squeeze_value_action_{XR_NULL_HANDLE};

    // Per-hand paths, pose spaces, and currently bound interaction profiles.
    std::array<XrPath, 2>             hand_paths_{XR_NULL_PATH, XR_NULL_PATH};
    std::array<XrSpace, 2>            grip_spaces_{XR_NULL_HANDLE, XR_NULL_HANDLE};
    std::array<XrSpace, 2>            aim_spaces_{XR_NULL_HANDLE, XR_NULL_HANDLE};
    std::array<controller_profile, 2> interaction_profiles_{controller_profile::none, controller_profile::none};

    // Runtime session/view state updated by pump_events() and query_views().
    XrViewConfigurationType                view_configuration_type_{XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO};
    XrEnvironmentBlendMode                 blend_mode_{XR_ENVIRONMENT_BLEND_MODE_OPAQUE};
    XrSessionState                         session_state_{XR_SESSION_STATE_UNKNOWN};
    bool                                   session_running_{false};
    bool                                   exit_requested_{false};
    bool                                   interaction_profiles_dirty_{true};
    std::array<XrViewConfigurationView, 2> view_configuration_views_{};
    std::array<XrView, 2>                  located_views_{};
    bool                                   located_views_valid_{false};

    // Runtime-owned eye swapchains.
    std::array<swapchain_view, 2> swapchain_views_{};
    std::int64_t                  swapchain_format_{0};

    // Producer rings for pixels and optional overlays.
    mapped_file pixel_mapping_;
    mapped_file overlay_mapping_;
    mapped_file modal_mapping_;

    // Double-buffered source/modal textures and the last complete frame snapshot.
    std::array<std::array<GLuint, 2>, 2>  source_textures_{};
    std::array<GLuint, 2>                 modal_textures_{};
    int                                   active_texture_set_{-1};
    std::uint32_t                         source_width_{0};
    std::uint32_t                         source_height_{0};
    std::uint64_t                         active_stereo_frame_id_{0};
    bool                                  active_stereo_frame_valid_{false};
    data_format::stereo_image_origin      active_origin_{data_format::stereo_image_origin::upper_left};
    data_format::stereo_presentation_mode active_presentation_mode_{data_format::stereo_presentation_mode::stereo_fullscreen};
    std::array<data_format::stereo_render_view, 2> active_render_views_{};
    std::array<std::vector<float>, 2>              active_overlay_commands_{};
    data_format::stereo_modal_overlay              active_modal_{};
    std::array<float, 16>                          world_panel_model_{};
    bool                                           world_panel_initialized_{false};

    // OpenGL resources used to draw Boba content into acquired OpenXR images.
    GLuint framebuffer_{0};
    GLuint panel_program_{0};
    GLuint panel_vao_{0};
    GLint  panel_source_location_{-1};
    GLint  panel_mvp_location_{-1};
    GLint  panel_flip_y_location_{-1};
    GLuint overlay_program_{0};
    GLuint overlay_vao_{0};
    GLuint overlay_vbo_{0};
    GLint  overlay_source_size_location_{-1};
    GLuint modal_program_{0};
    GLuint modal_vao_{0};
    GLuint modal_vbo_{0};
    GLint  modal_source_size_location_{-1};
    GLint  modal_texture_location_{-1};

    // Sequence number shared by controller/view events and previous state used for logging.
    std::uint64_t    sequence_{0};
    controller_input previous_input_{};
    bool             have_previous_input_{false};
};

} // namespace ILLIXR
