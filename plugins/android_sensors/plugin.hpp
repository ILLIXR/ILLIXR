#pragma once

#include "illixr/data_format/semantics.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/plugin.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"
// clang-format off
#include <IUnityInterface.h>
#include <IUnityRenderingExtensions.h>
#include <IUnityGraphicsVulkan.h>
// clang-format on
#include <camera/NdkCameraCaptureSession.h>
#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraManager.h>
#include <camera/NdkCameraMetadata.h>
#include <cstdint>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <mutex>
#include <vector>
// clang-format off
#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
// clang-format on

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
 *   ILLIXR_ENCODER_IFRAME_SEC  IDR period in seconds       (default: 1)
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

    // Set from UnityPluginLoad - must be called before the plugin constructs.
    static void set_unity_interfaces(IUnityInterfaces* interfaces);

    // ---- Vulkan depth readback ----
    // Unity's Vulkan handles, set via UnityPluginLoad → set_unity_interfaces().
    static IUnityInterfaces*      s_unity_interfaces_;
    static IUnityGraphicsVulkan*  s_vk_interface_;

    bool init_vulkan();
    void destroy_vulkan();

protected:
    void        _p_one_iteration() override;
    skip_option _p_should_skip() override;

private:
    // ---- Configuration ----
    uint8_t capture_fps_{2};
    int32_t bitrate_bps_ = 5'000'000;
    int32_t iframe_sec_  = 1;
    // Set to true when any init step fails - no-ops _p_should_skip.
    bool    init_failed_ = false;

    // ---- Timing ----
    int64_t tick_interval_ns_ = 33'333'333LL; // ~30 Hz default
    int64_t last_tick_ns_     = 0;

    // ---- OpenXR ----
    bool init_openxr();
    void destroy_openxr();

    XrInstance  xr_instance_  = XR_NULL_HANDLE;
    XrSession   xr_session_   = XR_NULL_HANDLE;
    XrSpace     head_space_   = XR_NULL_HANDLE; //!< VIEW space (tracks head)
    XrSpace     local_space_  = XR_NULL_HANDLE; //!< LOCAL space (world-locked)
    // false when handles are borrowed from Unity - must not destroy on shutdown.
    bool        owns_xr_         = false;

    // XR_META_environment_depth function pointers.
    // All defined in standard openxr.h - no Meta SDK headers needed.
    PFN_xrCreateEnvironmentDepthProviderMETA           xr_create_depth_provider_   = nullptr;
    PFN_xrDestroyEnvironmentDepthProviderMETA          xr_destroy_depth_provider_  = nullptr;
    PFN_xrStartEnvironmentDepthProviderMETA            xr_start_depth_provider_    = nullptr;
    PFN_xrCreateEnvironmentDepthSwapchainMETA          xr_create_depth_swapchain_  = nullptr;
    PFN_xrDestroyEnvironmentDepthSwapchainMETA         xr_destroy_depth_swapchain_ = nullptr;
    PFN_xrEnumerateEnvironmentDepthSwapchainImagesMETA xr_enum_depth_images_       = nullptr;
    PFN_xrGetEnvironmentDepthSwapchainStateMETA        xr_get_depth_state_         = nullptr;
    PFN_xrAcquireEnvironmentDepthImageMETA             xr_acquire_depth_image_     = nullptr;

    XrEnvironmentDepthProviderMETA  depth_provider_      = XR_NULL_HANDLE;
    XrEnvironmentDepthSwapchainMETA depth_swapchain_     = XR_NULL_HANDLE;
    bool                            depth_ext_available_ = false;
    int32_t                         depth_swapchain_width_  = 0;
    int32_t                         depth_swapchain_height_ = 0;

    // Vulkan swapchain images - VkImage per slot, populated at swapchain creation.
    std::vector<VkImage> depth_vk_images_;

    // ---- Pose lookup via xrLocateSpace (RGB only) ----
    // Converts XrPosef to a row-major 4x4 float matrix in out_matrix[16].
    bool get_pose_at_timestamp(XrTime timestamp, float out_matrix[16]) const;

    VkDevice         vk_device_        = VK_NULL_HANDLE;
    VkPhysicalDevice vk_physical_      = VK_NULL_HANDLE;
    VkQueue          vk_queue_         = VK_NULL_HANDLE;
    uint32_t         vk_queue_family_  = 0;
    VkCommandPool    vk_cmd_pool_      = VK_NULL_HANDLE;
    VkCommandBuffer  vk_cmd_buf_       = VK_NULL_HANDLE;
    VkFence          vk_fence_         = VK_NULL_HANDLE;

    // Host-visible staging buffer - sized for one full depth frame.
    VkBuffer         vk_staging_buf_   = VK_NULL_HANDLE;
    VkDeviceMemory   vk_staging_mem_   = VK_NULL_HANDLE;
    VkDeviceSize     vk_staging_size_  = 0;

    // Find a memory type index satisfying required property flags.
    uint32_t find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags props) const;

    // ---- Depth buffer (written by Unity thread, read by threadloop) ----
    struct depth_frame_data {
        std::vector<uint8_t>           data;     // R16F, 2 bytes/pixel, top-down
        data_format::camera_intrinsics intrinsics;
        float                          near_z   = 0.f;
        float                          far_z    = 0.f;
        float                          pose[16] = {};
        XrTime                         timestamp = 0;
        bool                           valid     = false;
    };

    std::mutex       depth_mutex_;
    depth_frame_data depth_buffer_;  //!< Latest depth frame from Unity thread

    // ---- MediaCodec HEVC encoder (Surface-input mode) ----
    bool init_encoder(int32_t width, int32_t height);
    void destroy_encoder();
    void drain_encoder_output();

    AMediaCodec*   codec_          = nullptr;
    AMediaFormat*  codec_format_   = nullptr;
    ANativeWindow* encoder_window_ = nullptr;
    int32_t        enc_width_      = 0;
    int32_t        enc_height_     = 0;

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

    // ---- Pending encoded RGB frames ----
    struct pending_rgb {
        std::vector<uint8_t> encoded;
        XrTime               timestamp = 0;
    };
    std::vector<pending_rgb> pending_frames_;

    // ---- Switchboard output ----
    const std::shared_ptr<switchboard>                      switchboard_;
    switchboard::network_writer<data_format::semantic_data> writer_;
    int32_t                                                 frame_number_ = 0;
    float                                                   max_depth_m_;
};

} // namespace ILLIXR
