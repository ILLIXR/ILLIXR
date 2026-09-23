#pragma once
#ifdef USING_OPENXR
// Vulkan must be included before OpenXR so the platform defines are in scope.
#    ifndef XR_USE_GRAPHICS_API_VULKAN
#        define XR_USE_GRAPHICS_API_VULKAN
#    endif
#    ifndef XR_USE_PLATFORM_ANDROID
#        define XR_USE_PLATFORM_ANDROID
#    endif

#    include "illixr/data_format/frame.hpp"
#    include "illixr/data_format/latency_data.hpp"
#    include "illixr/data_format/misc.hpp"
#    include "illixr/data_format/poses/combined_pose.hpp"
#    include "illixr/data_format/vulkan_context.hpp"
#    ifdef ILLIXR_ENABLE_BOBA
#        include "illixr/stoplight.hpp"
#    endif
#    include "illixr/switchboard.hpp"
#    include "illixr/threadloop.hpp"
#    include "illixr/vk/vulkan_context_provider.hpp"
#    include "oxr_relay.hpp"
#    include "stereo_renderer.hpp"

#    include <openxr/openxr.h>
#    include <openxr/openxr_platform.h>
#    include <vulkan/vulkan.h>
#    include <vulkan/vulkan_android.h>

#include <android/bitmap.h>
#include <jni.h>
#include <mutex>

#    ifdef ILLIXR_DUMP_FRAMES
#        include "frame_dumper.hpp"
#    endif
#    define USING_APP_SPACEWARP

// Guard for extension name macro that may not be present in older OpenXR headers
#    ifndef XR_EXT_HAND_INTERACTION_EXTENSION_NAME
#        define XR_EXT_HAND_INTERACTION_EXTENSION_NAME "XR_EXT_hand_interaction"
#    endif
namespace ILLIXR {
/**
 * @class oxr_interface
 * @brief OpenXR interface plugin for ILLIXR on Quest 3
 *
 * This class handles:
 * 1. OpenXR session management
 * 2. Frame rendering via stereo_renderer
 * 3. Swapchain management for stereo display
 * 4. Depth submission via XR_KHR_composition_layer_depth extension
 * */
class oxr_interface
    : public threadloop
    , public vk::vulkan_context_provider {
public:
    [[maybe_unused]] oxr_interface(const std::string& name_, phonebook* pb_);
    ~oxr_interface() override;

    void _p_thread_setup() override;

    XrSession session() const {
        return session_;
    }

    XrInstance instance() const {
        return instance_;
    }

    void log_callback(const switchboard::ptr<const data_format::message_type>& datum);

protected:
    void _p_one_iteration() override;

    void start() override;

    void stop() override;

private:
    // Swapchains
    struct swapchain_info {
        XrSwapchain                             swapchain = XR_NULL_HANDLE;
        uint32_t                                width     = 0;
        uint32_t                                height    = 0;
        std::vector<XrSwapchainImageVulkan2KHR> images;
        VkFormat                                format = VK_FORMAT_R8G8B8A8_UNORM;
    };

    // Swapchain management
    void create_swapchains();

    // OpenXR lifecycle
    void init_xr();
    void create_session();
    void poll_events();
    void run_frame();

    // ==================== Network Config Panel ====================
    // Runs entirely inside the constructor, before create_swapchains() or _p_thread_setup(),
    // since the network backend plugins that need the resulting env vars are constructed
    // immediately after this plugin returns from its constructor.

    /// Constructs the Java NetworkConfigPanel via JNI, resolves its method IDs, creates the
    /// quad swapchain and the small Vulkan resources used to upload its bitmap. Does NOT place
    /// the quad -- that needs a valid XrTime, which isn't available until the first
    /// xrWaitFrame inside run_network_config_loop().
    void init_network_config_panel();

    /// One-time placement of the quad ~0.6m in front of wherever the user was looking when the
    /// panel opened, using the first valid predicted display time from run_network_config_loop().
    void initialize_quad_pose(XrTime time);

    /// Blocks (via its own xrWaitFrame/xrBeginFrame/xrEndFrame loop, submitting only the quad
    /// layer) until NetworkConfigPanel.isFinished() is true.
    void run_network_config_loop();

    /// Polls the poke_pose_ action for both hands, hit-tests against the quad's plane, and
    /// calls NetworkConfigPanel.handlePoke() on down/up transitions. Also updates the fingertip
    /// depth markers' live pose/visibility (see create_fingertip_markers()), and checks the
    /// pinch gesture value at this same (poke-derived) position -- see the comment inside the
    /// per-hand loop in the .cpp for why pinch deliberately reuses poke's position rather than
    /// a separate aim-ray target.
    void update_network_config_input(XrTime predicted_display_time);

    /// Transforms a world-space (local_space_) position into the quad's own local frame.
    /// Shared by the poke hit-test and the pinch-select ray-plane intersection so both use
    /// identical (and identically-verified-or-not) axis conventions rather than duplicated math.
    void world_to_quad_local(float world_x, float world_y, float world_z,
                             float& local_x, float& local_y, float& local_z) const;

    /// Locks the given Bitmap's pixels via the NDK Bitmap API and copies them into the quad
    /// swapchain's current image via a small dedicated command buffer.
    void upload_bitmap_to_quad_swapchain(jobject bitmap);

    /// Shared Vulkan upload path (staging buffer -> barrier -> copy -> barrier -> submit).
    /// Takes the command pool/buffer/fence and staging buffer/memory as parameters rather than
    /// hardcoding a specific set, since it's used both by the network config panel (its own,
    /// panel-sized resources, destroyed once the panel closes) and by the connection log display
    /// (separately-sized resources matching the eye swapchains' resolution, needed later and
    /// independently of whether the panel ever ran).
    void upload_pixels_to_swapchain_image(swapchain_info& sc, const uint8_t* src_pixels, uint32_t src_stride_bytes,
                                          VkCommandPool cmd_pool, VkCommandBuffer cmd_buffer, VkFence fence,
                                          VkBuffer staging_buf, VkDeviceMemory staging_mem);

    /// Creates two small (32x32) quad swapchains, one per hand, holding a static solid-dot
    /// texture uploaded once. Their poses are updated every frame in update_network_config_input()
    /// to the hand's actual, un-projected fingertip position, so the user can see via ordinary
    /// stereo depth perception how far they still need to reach to touch the panel -- unlike the
    /// 2D cursor baked into the panel bitmap, which is always flush with the panel's own depth.
    void create_fingertip_markers();

    /// Parses NetworkConfigPanel.getResult()'s pipe-delimited wire format and setenv()s the
    /// resulting values, mirroring the field layout used by the original dialog-based version.
    void apply_network_config_result(const std::string& result);

    /// Destroys the fingertip marker swapchains and releases the JNI global ref to the panel's
    /// Java object -- but deliberately NOT network_config_swapchain_ or its Vulkan upload
    /// resources, which are reused by the connection log display until the first valid frame
    /// arrives; see destroy_connection_log_display().
    void destroy_network_config_panel();

    // ==================== Connection Log Display ====================
    // Shown as a quad (reusing the network config panel's own swapchain, pose, and size) until
    // the first valid frame ever arrives from the network; see run_frame()'s current_frames_
    // validity check. Chosen over a full projection layer since the config panel phase itself
    // only ever submitted quad layers and never showed a "frozen background" problem -- a
    // quad-only submission appears to composite correctly on this runtime, and reusing the
    // panel's existing swapchain/resources avoids needing a second, separately-sized Vulkan
    // resource set just for this brief waiting period.

    /// One-time setup, called from _p_thread_setup() (the render thread -- a different thread
    /// than the one that ran start()/the network config panel, hence its own JNI attachment).
    /// Reuses network_config_swapchain_ and its Vulkan resources rather than allocating new
    /// ones; does nothing (logging why) if that swapchain isn't available for some reason.
    void init_connection_log_display();

    /// Builds the display text from connection_log_lines_ (under lock), updates the Java panel
    /// if it changed, and uploads its bitmap into the reused network_config_swapchain_.
    void render_connection_log_quad();

    /// Final teardown of network_config_swapchain_ and its Vulkan upload resources (deferred
    /// from destroy_network_config_panel(), which stopped destroying them so the log display
    /// could reuse them) plus the log display's own JNI state. Called once, the first time a
    /// valid frame ever arrives (see run_frame()) -- or from the destructor, if the app exits
    /// while still waiting for one. Idempotent, so it's safe to call from both places.
    void destroy_connection_log_display();

    // ==================== Member Variables ====================

    const std::shared_ptr<switchboard>    switchboard_;
    struct android_app*                   app_;
    const std::shared_ptr<relative_clock> clock_;
#    ifdef ILLIXR_ENABLE_BOBA
    const std::shared_ptr<stoplight> stoplight_;
#    endif

    // Frame reading
    switchboard::reader<data_format::dual_frames> frame_reader_;
#    ifdef ILLIXR_ENABLE_BOBA
    // Host lifecycle message delivered over the reliable network backend.
    switchboard::reader<switchboard::event_wrapper<std::string>> boba_client_control_reader_;

#    endif

    std::shared_ptr<const data_format::dual_frames> current_frames_ = nullptr;
#    ifdef ILLIXR_ENABLE_BOBA
    bool client_shutdown_requested_{false};
#    endif

    // OpenXR handles
    XrSession               session_         = XR_NULL_HANDLE;
    XrInstance              instance_        = XR_NULL_HANDLE;
    XrSystemId              system_id_       = XR_NULL_SYSTEM_ID;
    XrSpace                 local_space_     = XR_NULL_HANDLE;
    XrSpace                 view_space_      = XR_NULL_HANDLE;
    XrSessionState          session_state_   = XR_SESSION_STATE_UNKNOWN;
    XrBool32                session_running_ = XR_FALSE;
    XrViewConfigurationView view_configs_[2]{};
    XrView                  views_[2]{};
#    ifdef ILLIXR_ENABLE_BOBA
    // World-panel anchor persists across head motion until presentation mode changes.
    bool                                  world_panel_anchor_initialized_{false};
    XrPosef                               world_panel_pose_{};
    data_format::stereo_presentation_mode previous_presentation_mode_{data_format::stereo_presentation_mode::stereo_fullscreen};
#    endif

    // Vulkan device objects
    VkInstance       vk_instance_        = VK_NULL_HANDLE;
    VkPhysicalDevice vk_physical_device_ = VK_NULL_HANDLE;
    VkDevice         vk_device_          = VK_NULL_HANDLE;
    VkQueue          vk_queue_           = VK_NULL_HANDLE;
    uint32_t         vk_queue_family_    = 0;

    // Published to the phonebook after create_session() so other plugins
    // (e.g. offload_rendering_client) can obtain the Vulkan context without
    // a direct dependency on oxr_interface.
    std::shared_ptr<data_format::vulkan_device_context> vk_context_;
    std::vector<XrApiLayerProperties>                   layer_properties_;
    std::vector<const char*>                            required_extensions_;
    std::vector<XrExtensionProperties>                  extension_properties_;

    std::array<swapchain_info, 2> swapchains_{};

    // Per-eye depth swapchains (only valid when use_depth_ && depth_extension_supported_)
    std::array<swapchain_info, 2> depth_swapchains_{};
#    ifndef USING_APP_SPACEWARP
    VkFormat depth_format_ = VK_FORMAT_D16_UNORM;
#    else
    VkFormat depth_format_ = VK_FORMAT_D32_SFLOAT;

    // One motion-vector swapchain per eye (432×432, R16G16B16A16_SFLOAT).
    // The App Spacewarp runtime consumes these to extrapolate intermediate frames.
    std::array<swapchain_info, 2> mv_swapchains_{};
    VkFormat                      mv_swapchain_format_ = VK_FORMAT_R16G16B16A16_SFLOAT;
#    endif

    // Rendering
    std::unique_ptr<stereo_renderer> renderer_;

    // Previous frame's dual_frames (held until GPU finishes)
    std::shared_ptr<const data_format::dual_frames> prev_frames_ = nullptr;

#    ifdef ILLIXR_DUMP_FRAMES
    std::unique_ptr<frame_dumper> dumper_[2];
#    endif
    uint64_t frame_counter_{0};

    // ==================== Depth Extension State ====================

    /// Whether to use depth submission (from environment variable)
    bool use_depth_{false};

    /// Whether XR_KHR_composition_layer_depth extension is supported
    bool depth_extension_supported_{false};

    // App Spacewarp (XR_FB_space_warp)
    // True when the runtime reports XR_FB_SPACE_WARP_EXTENSION_NAME.
    bool spacewarp_supported_{false};

    std::shared_ptr<oxr_relay> oxr_relay_;
    // std::atomic<uint64_t> next_frame_id_{0};
#    ifdef ILLIXR_ENABLE_BOBA
    int headset_width_  = NATIVE_STREAM_EYE_WIDTH;
    int headset_height_ = NATIVE_STREAM_EYE_HEIGHT;
#    else

    int    headset_width_  = HEADSET_WIDTH;
    int    headset_height_ = HEADSET_HEIGHT;
    double overscan_;
#    endif

    bool use_tcp_{false};
    bool use_udp_{false};
    // ==================== Network Config Panel ====================

    // JNI bridge to com.example.ILLIXR.ILLIXRNativeActivity$NetworkConfigPanel.
    // Cached for the duration of the config-gathering loop; all JNI calls happen from the same
    // thread that runs the oxr_interface constructor, so caching env_ here (rather than
    // re-resolving it per call) is safe.
    JNIEnv* network_config_env_        = nullptr;
    bool    network_config_did_attach_ = false;
    jclass  network_config_panel_class_ = nullptr;
    jobject network_config_panel_       = nullptr;
    jmethodID panel_render_method_       = nullptr;
    jmethodID panel_get_bitmap_method_   = nullptr;
    jmethodID panel_handle_poke_method_  = nullptr;
    jmethodID panel_update_hover_method_ = nullptr;
    jmethodID panel_is_finished_method_  = nullptr;
    jmethodID panel_is_confirmed_method_ = nullptr;
    jmethodID panel_get_result_method_   = nullptr;
    jmethodID panel_get_width_method_    = nullptr;
    jmethodID panel_get_height_method_   = nullptr;

    // Poke pose comes from oxr_relay_'s own XR_EXT_hand_interaction action set (it's declared
    // `friend oxr_interface`), rather than a second action set here: a session only permits a
    // single xrAttachSessionActionSets call in its lifetime, and oxr_relay_->initialize()
    // (called from this class's constructor, before start() runs) already makes that call for
    // its own aim/grip/pinch/poke actions -- which already include poke, per hand, on exactly
    // the same interaction profile this would otherwise have duplicated.
    /// Debounced poke engagement state per hand; see update_network_config_input().
    bool poke_engaged_[2] = {false, false};

    /// Debounced pinch-select engagement state per hand; see the pinch check inside
    /// update_network_config_input()'s per-hand loop.
    bool pinch_engaged_[2] = {false, false};

    // Two small (32x32) quad swapchains holding a static dot texture, one per hand, whose
    // pose is updated every frame in update_network_config_input() to the hand's actual
    // (un-projected) fingertip position when tracked, or a fixed off-screen "parked" position
    // when not -- see create_fingertip_markers() and parked_marker_pose(). Always submitted
    // (never omitted from a frame's layer list), since varying which layers are submitted
    // frame-to-frame is what caused a freeze; see the comment on parked_marker_pose() in
    // plugin.cpp.
    swapchain_info fingertip_marker_swapchain_[2];
    XrPosef        fingertip_marker_pose_[2]{};

    // Quad layer swapchain for the panel; reuses the same swapchain_info shape as the eye
    // swapchains even though it only needs a single (non-array) image.
    swapchain_info network_config_swapchain_;
    XrPosef         network_config_quad_pose_{};
    // Reduced 25% per feedback ("size of everything too large"). Pixel dimensions
    // (PANEL_WIDTH_PX/PANEL_HEIGHT_PX in NetworkConfigPanel.java) are unchanged, so this just
    // increases effective DPI -- everything drawn on the panel shrinks uniformly along with it,
    // without needing any Java-side layout changes.
    float           network_config_quad_width_m_  = 0.45f;   // was 0.6f
    float           network_config_quad_height_m_ = 0.4875f; // was 0.65f

    // Minimal Vulkan resources for uploading the panel's bitmap into the quad swapchain image.
    // Deliberately separate from stereo_renderer_, which isn't constructed until
    // _p_thread_setup() runs on the (different) render thread -- this upload happens earlier,
    // synchronously inside the constructor.
    VkCommandPool   network_config_cmd_pool_    = VK_NULL_HANDLE;
    VkCommandBuffer network_config_cmd_buffer_  = VK_NULL_HANDLE;
    VkFence         network_config_fence_       = VK_NULL_HANDLE;
    VkBuffer        network_config_staging_buf_ = VK_NULL_HANDLE;
    VkDeviceMemory  network_config_staging_mem_ = VK_NULL_HANDLE;
    VkDeviceSize    network_config_staging_size_ = 0;

    std::vector<std::string> log_messages_;
    std::mutex log_mtx_;

    // ==================== Connection Log Display ====================

    /// Set once current_frames_ is ever valid; gates run_frame()'s choice between the log
    /// display and real rendering. Deliberately a one-way latch (never reset to false), so a
    /// later transient invalid frame doesn't flicker the display back to the log screen.
    bool first_valid_frame_received_ = false;

    /// Avoids rebuilding/re-sending the same joined text to Java every frame when nothing new
    /// has been logged since the last check.
    std::string              last_sent_connection_log_;

    // JNI bridge to com.example.ILLIXR.ILLIXRNativeActivity$LogDisplayPanel. Uses its own
    // JNIEnv*, separate from network_config_env_: this all runs on the render thread (spawned by
    // threadloop::start()), a different thread than the one that ran start() and the network
    // config panel, and a JNIEnv* is only valid on the thread it was obtained on.
    JNIEnv* render_thread_env_        = nullptr;
    bool    render_thread_did_attach_ = false;
    jobject log_display_panel_        = nullptr;
    jmethodID log_panel_set_text_method_   = nullptr;
    jmethodID log_panel_render_method_     = nullptr;
    jmethodID log_panel_get_bitmap_method_ = nullptr;

    // No separate Vulkan resources here: the log display reuses network_config_swapchain_ and
    // network_config_cmd_pool_/cmd_buffer_/fence_/staging_buf_/staging_mem_ (declared above),
    // which destroy_network_config_panel() deliberately leaves alive for exactly this purpose.
};

} // namespace ILLIXR
#endif
