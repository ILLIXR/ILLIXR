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
};

} // namespace ILLIXR
#endif
