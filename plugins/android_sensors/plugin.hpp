#pragma once

#include "illixr/data_format/query_response.hpp"
#include "illixr/data_format/semantics.hpp"
#include "illixr/data_format/voice_query.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/plugin.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"
// clang-format off

#include "ndk_encoder.hpp"

#include <IUnityInterface.h>
#include <IUnityRenderingExtensions.h>
#include <IUnityGraphicsVulkan.h>
// clang-format on
#include <array>
#include <camera/NdkCameraCaptureSession.h>
#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraManager.h>
#include <camera/NdkCameraMetadata.h>
#include <cstdint>
#include <mutex>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <vector>
#include <vulkan/vulkan.h>

namespace ILLIXR {

/**
 * @brief ILLIXR plugin that co-captures HEVC-encoded RGB frames, depth frames,
 *        and per-sensor matched head poses on the Meta Quest 3.
 *
 * Acquisition and encoding pipeline
 * ----------------------------------
 * RGB:   Camera2 writes directly into MediaCodec's ANativeWindow input surface.
 *        The Snapdragon hardware handles NV12 → HEVC without any CPU copy.
 *        The encoder is initialised first so its ANativeWindow can be used as
 *        the Camera2 capture target. Completed H.265 output buffers are drained
 *        on each threadloop tick.
 *        RGB pose: xrLocateSpace() at the encoder output presentationTimeUs.
 *
 * Depth: XR_META_environment_depth OpenXR extension (standard Khronos openxr.h).
 *        Unity uses the Vulkan renderer, so depth swapchain images are VkImages.
 *        acquire_depth_unity_thread() is called from LateUpdate() via ILLIXRBridge
 *        while Unity's XR frame is open. It acquires the depth VkImage and copies
 *        it to a host-visible staging buffer via vkCmdCopyImageToBuffer, then maps
 *        the buffer to read the R16F pixels to CPU. Vulkan handles are obtained
 *        from IUnityGraphicsVulkan via UnityPluginLoad().
 *        Depth format: R16F (float16), 2 bytes/pixel, top-down, inverted NDC.
 *        depth_m = near_z / r_value.
 *
 * Initialisation order
 * --------------------
 *   UnityPluginLoad()  - captures IUnityGraphicsVulkan (called by Unity at startup)
 *   1. init_openxr()   - reuses Unity's XrInstance/XrSession, loads depth extension
 *   2. init_vulkan()   - creates staging buffer, command pool, fence from Unity's VkDevice
 *   3. init_encoder()  - MediaCodec HEVC, creates input ANativeWindow
 *   4. init_camera()   - Camera2, targeting encoder's ANativeWindow
 *
 * Configuration (env vars, set via Unity JNI bridge before plugin start)
 * -----------------------------------------------------------------------
 *   ILLIXR_CAPTURE_FPS         Encoder target FPS hint     (default: 30)
 *   ILLIXR_ENCODER_BITRATE_BPS HEVC bitrate in bits/s      (default: 5000000)
 *   ILLIXR_DEPTH_DISABLED      Set to 1 to skip depth      (default: 0)
 *   ILLIXR_MAX_DEPTH_M         Far-depth cap in metres     (default: 0)
 *
 * Publishes: "semantic_data" (ILLIXR::data_format::semantic_data)
 */
class xr_sensor_capture : public threadloop {
public:
    [[maybe_unused]] xr_sensor_capture(const std::string& name, phonebook* pb);
    ~xr_sensor_capture() override;

    // Called from Unity's LateUpdate() via illixr_acquire_depth() in ILLIXRBridge.
    // Must be called between xrBeginFrame and xrEndFrame (i.e. during Unity's frame).
    void acquire_depth_unity_thread();

    // Public so on_render_event callback can call them from outside the class.
    bool init_vulkan();
    void destroy_vulkan();

    // Set from UnityPluginLoad - must be called before the plugin constructs.
    static void set_unity_interfaces(IUnityInterfaces* interfaces);

    // Public so UnityPluginLoad/UnityPluginUnload free functions and the
    // on_render_event callback can access them from outside the class.
    static IUnityInterfaces*     s_unity_interfaces_;
    static IUnityGraphicsVulkan* s_vk_interface_;

    // Called from render thread via GL.IssuePluginEvent(EVENT_ACQUIRE).
    // Submits the Vulkan cmd buffer prepared by acquire_depth_unity_thread().
    void submit_depth_readback();

    // Called from LateUpdate() on the main thread immediately after
    // GL.IssuePluginEvent(EVENT_ACQUIRE) returns. Releases the depth
    // image back to the OpenXR runtime before xrEndFrame.
    // Must be called on the main thread - same constraint as xrBeginFrame.
    void release_depth_after_submit();

protected:
    void        _p_one_iteration() override;
    skip_option _p_should_skip() override;

private:
    // ---- Configuration ----
    // Set to true when any init step fails - no-ops _p_should_skip.
    bool init_failed_ = false;

    // ---- Timing ----
    int64_t tick_interval_ns_ = 33'333'333LL;
    int64_t last_tick_ns_     = 0;

    // ---- OpenXR ----
    bool init_openxr();
    void destroy_openxr();

    XrInstance xr_instance_ = XR_NULL_HANDLE;
    XrSession  xr_session_  = XR_NULL_HANDLE;
    XrSpace    head_space_  = XR_NULL_HANDLE;
    XrSpace    local_space_ = XR_NULL_HANDLE;
    bool       owns_xr_     = false;

    PFN_xrCreateEnvironmentDepthProviderMETA           xr_create_depth_provider_   = nullptr;
    PFN_xrDestroyEnvironmentDepthProviderMETA          xr_destroy_depth_provider_  = nullptr;
    PFN_xrStartEnvironmentDepthProviderMETA            xr_start_depth_provider_    = nullptr;
    PFN_xrCreateEnvironmentDepthSwapchainMETA          xr_create_depth_swapchain_  = nullptr;
    PFN_xrDestroyEnvironmentDepthSwapchainMETA         xr_destroy_depth_swapchain_ = nullptr;
    PFN_xrEnumerateEnvironmentDepthSwapchainImagesMETA xr_enum_depth_images_       = nullptr;
    PFN_xrGetEnvironmentDepthSwapchainStateMETA        xr_get_depth_state_         = nullptr;
    PFN_xrAcquireEnvironmentDepthImageMETA             xr_acquire_depth_image_     = nullptr;
    // PFN_xrReleaseEnvironmentDepthImageMETA takes only the provider handle.
    using xr_release_depth_fn                   = XrResult(XRAPI_PTR*)(XrEnvironmentDepthProviderMETA);
    xr_release_depth_fn xr_release_depth_image_ = nullptr;

    bool needs_depth_release_ = false; // true after acquire, until release

    XrEnvironmentDepthProviderMETA  depth_provider_         = XR_NULL_HANDLE;
    XrEnvironmentDepthSwapchainMETA depth_swapchain_        = XR_NULL_HANDLE;
    bool                            depth_ext_available_    = false;
    int32_t                         depth_swapchain_width_  = 0;
    int32_t                         depth_swapchain_height_ = 0;

    // Vulkan swapchain images - VkImage per slot, populated at swapchain creation.
    std::vector<VkImage> depth_vk_images_;

    // ---- Pose lookup ----
    bool get_pose_at_timestamp(XrTime timestamp, float out_matrix[16]) const;

    VkDevice         vk_device_       = VK_NULL_HANDLE;
    VkPhysicalDevice vk_physical_     = VK_NULL_HANDLE;
    VkQueue          vk_queue_        = VK_NULL_HANDLE;
    uint32_t         vk_queue_family_ = 0;
    VkCommandPool    vk_cmd_pool_     = VK_NULL_HANDLE;
    VkCommandBuffer  vk_cmd_buf_      = VK_NULL_HANDLE;
    VkFence          vk_fence_        = VK_NULL_HANDLE;

    // Host-visible staging buffer - sized for one full depth frame.
    VkBuffer       vk_staging_buf_  = VK_NULL_HANDLE;
    VkDeviceMemory vk_staging_mem_  = VK_NULL_HANDLE;
    VkDeviceSize   vk_staging_size_ = 0;

    // Find a memory type index satisfying required property flags.
    uint32_t find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags props) const;

    // ---- Depth cache (written by Unity thread, read by threadloop) ----
    //
    // acquire_depth_unity_thread() is called every DEPTH_ACQUIRE_EVERY
    // LateUpdate() ticks (~10fps at 72Hz Unity) and stores frames in a
    // ring buffer of DEPTH_CACHE_SIZE entries. _p_one_iteration() picks
    // the entry whose timestamp is closest to each encoded RGB frame.
    static constexpr int    DEPTH_ACQUIRE_EVERY = 7;  // ~10fps at 72Hz
    static constexpr size_t DEPTH_CACHE_SIZE    = 16; // ~1.6s of history

    struct depth_frame_data {
        std::vector<uint8_t>           data; // R16F, 2 bytes/pixel, top-down
        data_format::camera_intrinsics intrinsics;
        float                          near_z    = 0.f;
        float                          far_z     = 0.f;
        float                          pose[16]  = {};
        XrTime                         timestamp = 0;
        bool                           valid     = false;
    };

    // Returns the cached entry with the smallest |timestamp - rgb_ts|,
    // or nullptr if the cache is empty.
    const depth_frame_data* find_closest_depth(XrTime rgb_ts) const;

    mutable std::mutex                             depth_mutex_;
    std::array<depth_frame_data, DEPTH_CACHE_SIZE> depth_cache_;
    size_t                                         depth_cache_next_      = 0;
    int                                            depth_acquire_counter_ = 0;

    // Pending Vulkan readback: set by acquire_depth_unity_thread() on the
    // main thread, consumed by submit_depth_readback() on the render thread.
    struct pending_readback {
        VkImage                        image  = VK_NULL_HANDLE;
        int32_t                        width  = 0;
        int32_t                        height = 0;
        data_format::camera_intrinsics intrinsics;
        float                          near_z    = 0.f;
        float                          far_z     = 0.f;
        float                          pose[16]  = {};
        XrTime                         timestamp = 0;
        bool                           valid     = false;
    };

    std::mutex       pending_readback_mutex_;
    pending_readback pending_readback_;

    // ---- Camera2 RGB ----
    bool init_camera();
    void destroy_camera();

    ACameraManager*                 camera_mgr_               = nullptr;
    ACameraDevice*                  camera_device_            = nullptr;
    ACameraCaptureSession*          capture_session_          = nullptr;
    ACaptureRequest*                capture_request_          = nullptr;
    ACameraOutputTarget*            camera_output_target_     = nullptr;
    ACaptureSessionOutputContainer* session_output_container_ = nullptr;
    ACaptureSessionOutput*          session_output_           = nullptr;

    data_format::camera_intrinsics rgb_intrinsics_;
    bool                           rgb_intrinsics_valid_ = false;

    // ---- Switchboard output ----
    const std::shared_ptr<switchboard>                       switchboard_;
    switchboard::network_writer<data_format::semantic_frame> writer_;
    int32_t                                                  frame_number_ = 0;
    float                                                    max_depth_m_;

    std::unique_ptr<ndk_encoder> encoder_;
    int64_t                      clock_offset_ns_;
};

} // namespace ILLIXR
