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
#    include "illixr/stoplight.hpp"
#    include "illixr/switchboard.hpp"
#    include "illixr/threadloop.hpp"
#    include "illixr/vk/vulkan_context_provider.hpp"
#    include "oxr_relay.hpp"
#    include "stereo_renderer.hpp"

#    include <openxr/openxr.h>
#    include <openxr/openxr_platform.h>
#    include <vulkan/vulkan.h>
#    include <vulkan/vulkan_android.h>

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
    // Swapchain management
    void create_swapchains();

    // OpenXR lifecycle
    void init_xr();
    void create_session();
    void poll_events();
    void run_frame();

    // void rx_latency(const switchboard::ptr<const data_format::network_latency_result>& datum);

    // ==================== Member Variables ====================

    const std::shared_ptr<switchboard>    switchboard_;
    struct android_app*                   app_;
    const std::shared_ptr<relative_clock> clock_;
    const std::shared_ptr<stoplight>      stoplight_;

    // Frame reading
    switchboard::reader<data_format::dual_frames> frame_reader_;
    switchboard::reader<switchboard::event_wrapper<std::string>> boba_client_control_reader_;

    std::shared_ptr<const data_format::dual_frames> current_frames_ = nullptr;
    bool                                             client_shutdown_requested_{false};

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
    bool                    world_panel_anchor_initialized_{false};
    XrPosef                 world_panel_pose_{};
    data_format::stereo_presentation_mode previous_presentation_mode_{
        data_format::stereo_presentation_mode::stereo_fullscreen};

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

    // Swapchains
    struct swapchain_info {
        XrSwapchain                             swapchain = XR_NULL_HANDLE;
        uint32_t                                width     = 0;
        uint32_t                                height    = 0;
        std::vector<XrSwapchainImageVulkan2KHR> images;
        VkFormat                                format = VK_FORMAT_R8G8B8A8_UNORM;
    };

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
};

} // namespace ILLIXR
#endif
