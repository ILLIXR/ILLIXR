#include "plugin.hpp"

#include "illixr/data_format/serialization/query_response.hpp"
#include "illixr/data_format/serialization/voice_query.hpp"
#include "illixr/error_util.hpp"

#include <algorithm>
#include <android/log.h>
#include <camera/NdkCameraMetadataTags.h>
#include <cmath>
#include <cstring>
#include <media/NdkMediaError.h>
#include <mutex>
#include <string>
#include <vulkan/vulkan.h>

using namespace ILLIXR;
using namespace ILLIXR::data_format;

// Forward declaration so destroy_openxr() and _p_one_iteration() can reference
// this before acquire_depth_unity_thread() is defined.
static xr_sensor_capture* g_sensor_capture_instance = nullptr;

// Static members for Unity plugin interface.
IUnityInterfaces*     xr_sensor_capture::s_unity_interfaces_ = nullptr;
IUnityGraphicsVulkan* xr_sensor_capture::s_vk_interface_     = nullptr;

// ---------------------------------------------------------------------------
// Unity plugin lifecycle - called by Unity when the native library is loaded.
// Must be exported so Unity can find it by name.
// ---------------------------------------------------------------------------

void xr_sensor_capture::set_unity_interfaces(IUnityInterfaces* interfaces) {
    s_unity_interfaces_ = interfaces;
    if (interfaces != nullptr) {
        s_vk_interface_ = interfaces->Get<IUnityGraphicsVulkan>();
        if (s_vk_interface_ == nullptr) {
            __android_log_print(ANDROID_LOG_ERROR, "xr_sensor_capture",
                                "IUnityGraphicsVulkan not available - is Unity using Vulkan?");
        } else {
            __android_log_print(ANDROID_LOG_INFO, "xr_sensor_capture", "IUnityGraphicsVulkan acquired");
        }
    }
}

// ---------------------------------------------------------------------------
// Render thread callback - fired via GL.IssuePluginEvent from C#.
//
// This runs on Unity's render thread where the Vulkan device is current and
// safe to use. We use it to initialize Vulkan readback resources after the
// xr_sensor_capture plugin has been loaded by ILLIXR. Two event IDs:
//   EVENT_INIT   (0): acquire IUnityGraphicsVulkan and init_vulkan()
//   EVENT_UNINIT (1): destroy_vulkan() and release interface
// ---------------------------------------------------------------------------
static constexpr int EVENT_INIT    = 0;
static constexpr int EVENT_UNINIT  = 1;
static constexpr int EVENT_ACQUIRE = 2; // run acquire_depth on render thread

static void UNITY_INTERFACE_API on_render_event(int event_id) {
    if (event_id == EVENT_INIT) {
        if (ILLIXR::xr_sensor_capture::s_unity_interfaces_ == nullptr) {
            __android_log_print(ANDROID_LOG_ERROR, "xr_sensor_capture", "on_render_event(INIT): s_unity_interfaces_ is null");
            return;
        }
        ILLIXR::xr_sensor_capture::s_vk_interface_ =
            ILLIXR::xr_sensor_capture::s_unity_interfaces_->Get<IUnityGraphicsVulkan>();
        if (ILLIXR::xr_sensor_capture::s_vk_interface_ == nullptr) {
            __android_log_print(ANDROID_LOG_ERROR, "xr_sensor_capture",
                                "on_render_event(INIT): IUnityGraphicsVulkan not available");
            return;
        }
        if (g_sensor_capture_instance != nullptr) {
            if (g_sensor_capture_instance->init_vulkan()) {
                __android_log_print(ANDROID_LOG_INFO, "xr_sensor_capture",
                                    "on_render_event(INIT): Vulkan resources initialized");
            }
        } else {
            // Plugin not yet constructed - interfaces stored, init_vulkan()
            // will be called when the plugin constructs and finds s_vk_interface_ set.
            __android_log_print(ANDROID_LOG_INFO, "xr_sensor_capture",
                                "on_render_event(INIT): interfaces stored, waiting for plugin construction");
        }
    } else if (event_id == EVENT_UNINIT) {
        if (g_sensor_capture_instance != nullptr)
            g_sensor_capture_instance->destroy_vulkan();
        ILLIXR::xr_sensor_capture::s_vk_interface_ = nullptr;
    } else if (event_id == EVENT_ACQUIRE) {
        // submit_depth_readback runs on the render thread where Unity's
        // graphics queue is exclusively owned - safe to call vkQueueSubmit.
        // acquire_depth_unity_thread() already ran on the main thread via
        // illixr_acquire_depth() and stored the pending readback parameters.
        if (g_sensor_capture_instance != nullptr) {
            g_sensor_capture_instance->submit_depth_readback();
            // Release must happen before xrEndFrame - do it here
            // while still on the render thread after submit completes.
            g_sensor_capture_instance->release_depth_after_submit();
        }
    }
}

extern "C" {
// Called by Unity at library load time - stores the interfaces pointer so
// on_render_event can retrieve IUnityGraphicsVulkan on the render thread.
UNITY_INTERFACE_EXPORT void UNITY_INTERFACE_API UnityPluginLoad(IUnityInterfaces* interfaces) {
    xr_sensor_capture::set_unity_interfaces(interfaces);
}

UNITY_INTERFACE_EXPORT void UNITY_INTERFACE_API UnityPluginUnload() {
    xr_sensor_capture::set_unity_interfaces(nullptr);
}

// Returns the render event callback pointer for GL.IssuePluginEvent.
// C# calls this once and caches the result.
UNITY_INTERFACE_EXPORT UnityRenderingEvent UNITY_INTERFACE_API illixr_get_render_event_callback() {
    return on_render_event;
}

extern "C" void UNITY_INTERFACE_API illixr_release_depth() {
    if (g_sensor_capture_instance != nullptr)
        g_sensor_capture_instance->release_depth_after_submit();
}
} // extern "C"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static XrTime clock_boottime_xr() {
    struct timespec ts{};
    clock_gettime(CLOCK_BOOTTIME, &ts);
    return static_cast<XrTime>(static_cast<int64_t>(ts.tv_sec) * 1'000'000'000LL + ts.tv_nsec);
}

// ---------------------------------------------------------------------------
// Constructor / destructor
// ---------------------------------------------------------------------------

[[maybe_unused]] xr_sensor_capture::xr_sensor_capture(const std::string& name, phonebook* pb)
    : threadloop{name, pb}
    , switchboard_{phonebook_->lookup_impl<switchboard>()}
    , writer_{switchboard_->get_network_writer<semantic_frame>("semantic_frame", {})} {
    uint8_t capture_fps = switchboard_->get_env_int("ILLIXR_CAPTURE_FPS", 2);
    int32_t bitrate_bps = switchboard_->get_env_int("ILLIXR_ENCODER_BITRATE_BPS", 5'000'000);
    max_depth_m_        = switchboard_->get_env_float("ILLIXR_CAPTURE_MAX_DEPTH", 0.f);
    tick_interval_ns_   = static_cast<int64_t>(1'000'000'000 / capture_fps);

    struct timespec mono{}, boot{};
    clock_gettime(CLOCK_MONOTONIC, &mono);
    clock_gettime(CLOCK_BOOTTIME, &boot);
    clock_offset_ns_ = (static_cast<int64_t>(boot.tv_sec) * 1'000'000'000LL + boot.tv_nsec) -
        (static_cast<int64_t>(mono.tv_sec) * 1'000'000'000LL + mono.tv_nsec);
    spdlog::get("illixr")->info("xr_sensor_capture init: fps={} bitrate={} iframe_sec={} max_depth={} ", capture_fps,
                                bitrate_bps, max_depth_m_);
    // Use init_failed_ instead of throwing - throwing from a threadloop-derived
    // constructor triggers the threadloop destructor assertion because the
    // stoplight was never started. Degrade gracefully and no-op in _p_should_skip.
    if (!init_openxr()) {
        spdlog::get("illixr")->error("xr_sensor_capture: OpenXR init failed - plugin will no-op");
        init_failed_ = true;
        return;
    }

    // Lazily create and start the depth provider on first tick.
    // xrCreateEnvironmentDepthProviderMETA requires a running session -
    // Unity's session is already running by the time ILLIXR starts.
    if (depth_ext_available_ && depth_provider_ == XR_NULL_HANDLE) {
        XrEnvironmentDepthProviderCreateInfoMETA prov_ci{XR_TYPE_ENVIRONMENT_DEPTH_PROVIDER_CREATE_INFO_META};
        prov_ci.createFlags = 0;
        XrResult r          = xr_create_depth_provider_(xr_session_, &prov_ci, &depth_provider_);
        if (XR_FAILED(r)) {
            spdlog::get("illixr")->error("xrCreateEnvironmentDepthProviderMETA failed: {}", static_cast<int>(r));
            depth_ext_available_ = false;
        } else {
            xr_start_depth_provider_(depth_provider_);
            spdlog::get("illixr")->info("Depth provider created and started");
        }
    }

    // Vulkan init: if IUnityGraphicsVulkan is already available (render event
    // fired before this constructor ran), init now. Otherwise on_render_event
    // will call init_vulkan() when it fires. Either way, acquire_depth_unity_thread()
    // checks vk_device_ != VK_NULL_HANDLE before proceeding.
    if (s_vk_interface_ != nullptr) {
        if (!init_vulkan()) {
            spdlog::get("illixr")->error("xr_sensor_capture: Vulkan init failed - depth will be unavailable");
            // Do not set init_failed_ - RGB capture still works without depth.
        }
    } else {
        spdlog::get("illixr")->warn("xr_sensor_capture: IUnityGraphicsVulkan not yet available - "
                                    "depth init deferred to render event callback");
    }

    // Encoder dimensions - must match what Camera2 delivers.
    // Quest 3 back camera common resolutions: 1280x960, 1920x1440, 2560x1920.
    // Override via env vars if the default does not match hardware.
    const int32_t camera_w = switchboard_->get_env_int("ILLIXR_CAMERA_WIDTH", 960);
    const int32_t camera_h = switchboard_->get_env_int("ILLIXR_CAMERA_HEIGHT", 960);
    spdlog::get("illixr")->info("xr_sensor_capture: encoder target {}x{} "
                                "(set ILLIXR_CAMERA_WIDTH/HEIGHT to override)",
                                camera_w, camera_h);
    encoder_ = std::make_unique<ndk_encoder>(camera_w, camera_h, bitrate_bps, capture_fps);
    if (!encoder_->initialize(switchboard_->get_env_int("ILLIXR_ENCODER_IFRAME_SEC", 1))) {
        spdlog::get("illixr")->error("xr_sensor_capture: encoder init failed - plugin will no-op");
        init_failed_ = true;
        return;
    }

    if (!init_camera()) {
        spdlog::get("illixr")->error("xr_sensor_capture: Camera2 init failed - plugin will no-op");
        init_failed_ = true;
        return;
    }

    // Register instance pointer for C-linkage functions and render event callback.
    g_sensor_capture_instance = this;
}

xr_sensor_capture::~xr_sensor_capture() {
    destroy_camera();
    encoder_->destroy_encoder();
    destroy_vulkan();
    destroy_openxr();
}

void xr_sensor_capture::release_depth_after_submit() {
    // Must be called on Unity's main thread - same thread that called
    // xrAcquireEnvironmentDepthImageMETA - before xrEndFrame closes the
    // frame. GL.IssuePluginEvent(EVENT_ACQUIRE) in LateUpdate() is
    // synchronous: the render thread finishes submit_depth_readback()
    // (including vkWaitForFences) before this returns to C#, so the
    // GPU readback is complete before we release the swapchain image.
    if (!needs_depth_release_)
        return;

    if (xr_release_depth_image_ != nullptr) {
        XrResult result = xr_release_depth_image_(depth_provider_);
        if (XR_FAILED(result)) {
            spdlog::get("illixr")->warn("xrReleaseEnvironmentDepthImageMETA failed: {}", static_cast<int>(result));
        }
    }
    needs_depth_release_ = false;
}

// ---------------------------------------------------------------------------
// OpenXR
// ---------------------------------------------------------------------------

bool xr_sensor_capture::init_openxr() {
    // Reuse Unity's existing XrInstance and XrSession if available.
    // The Quest 3 OpenXR loader does not support simultaneous XrInstances,
    // so creating our own would fail with XR_ERROR_LIMIT_REACHED (-10).
    // ILLIXRXrHandleProvider (C#) captures Unity's handles via OpenXRFeature
    // callbacks and passes them here via env vars before illixr_unity_init().
    const char* inst_str = getenv("ILLIXR_XR_INSTANCE");
    const char* sess_str = getenv("ILLIXR_XR_SESSION");

    if (inst_str && *inst_str && sess_str && *sess_str) {
        xr_instance_ = reinterpret_cast<XrInstance>(std::stoull(inst_str));
        xr_session_  = reinterpret_cast<XrSession>(std::stoull(sess_str));
        owns_xr_     = false;
        spdlog::get("illixr")->info("Reusing Unity XrInstance={} XrSession={}", reinterpret_cast<void*>(xr_instance_),
                                    reinterpret_cast<void*>(xr_session_));
    } else {
        spdlog::get("illixr")->error("ILLIXR_XR_INSTANCE / ILLIXR_XR_SESSION not set - "
                                     "cannot create a second XrInstance on Quest 3. "
                                     "Enable ILLIXRXrHandleProvider in OpenXR features.");
        return false;
    }

    XrReferenceSpaceCreateInfo space_ci{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    space_ci.poseInReferenceSpace = {{0.f, 0.f, 0.f, 1.f}, {0.f, 0.f, 0.f}};

    space_ci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    xrCreateReferenceSpace(xr_session_, &space_ci, &head_space_);

    space_ci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    xrCreateReferenceSpace(xr_session_, &space_ci, &local_space_);

    // Load all XR_META_environment_depth entry points from openxr.h PFN types.
    // These are part of the standard Khronos SDK - no Meta SDK headers needed.
    auto load = [&](const char* fname, PFN_xrVoidFunction* out) {
        return xrGetInstanceProcAddr(xr_instance_, fname, out) == XR_SUCCESS;
    };

    bool ok = true;
    ok &= load("xrCreateEnvironmentDepthProviderMETA", reinterpret_cast<PFN_xrVoidFunction*>(&xr_create_depth_provider_));
    ok &= load("xrDestroyEnvironmentDepthProviderMETA", reinterpret_cast<PFN_xrVoidFunction*>(&xr_destroy_depth_provider_));
    ok &= load("xrStartEnvironmentDepthProviderMETA", reinterpret_cast<PFN_xrVoidFunction*>(&xr_start_depth_provider_));
    ok &= load("xrCreateEnvironmentDepthSwapchainMETA", reinterpret_cast<PFN_xrVoidFunction*>(&xr_create_depth_swapchain_));
    ok &= load("xrDestroyEnvironmentDepthSwapchainMETA", reinterpret_cast<PFN_xrVoidFunction*>(&xr_destroy_depth_swapchain_));
    ok &= load("xrEnumerateEnvironmentDepthSwapchainImagesMETA", reinterpret_cast<PFN_xrVoidFunction*>(&xr_enum_depth_images_));
    ok &= load("xrGetEnvironmentDepthSwapchainStateMETA", reinterpret_cast<PFN_xrVoidFunction*>(&xr_get_depth_state_));
    ok &= load("xrAcquireEnvironmentDepthImageMETA", reinterpret_cast<PFN_xrVoidFunction*>(&xr_acquire_depth_image_));

    // xrReleaseEnvironmentDepthImageMETA is absent from the entry point map on
    // some Quest 3 firmware versions. Treat as non-fatal - if unavailable, the
    // runtime auto-releases on the next acquire call.
    load("xrReleaseEnvironmentDepthImageMETA", reinterpret_cast<PFN_xrVoidFunction*>(&xr_release_depth_image_));
    if (xr_release_depth_image_ == nullptr) {
        spdlog::get("illixr")->warn("xrReleaseEnvironmentDepthImageMETA unavailable - "
                                    "relying on runtime auto-release");
    }

    if (!ok) {
        spdlog::get("illixr")->warn("XR_META_environment_depth entry points missing - depth disabled");
        depth_ext_available_ = false;
    } else {
        depth_ext_available_ = true;
        spdlog::get("illixr")->info("XR_META_environment_depth loaded");
    }

    return true;
}

void xr_sensor_capture::destroy_openxr() {
    if (depth_swapchain_ != XR_NULL_HANDLE && xr_destroy_depth_swapchain_) {
        xr_destroy_depth_swapchain_(depth_swapchain_);
        depth_swapchain_ = XR_NULL_HANDLE;
    }
    if (depth_provider_ != XR_NULL_HANDLE && xr_destroy_depth_provider_) {
        xr_destroy_depth_provider_(depth_provider_);
        depth_provider_ = XR_NULL_HANDLE;
    }
    // Reference spaces are always ours to destroy.
    if (head_space_ != XR_NULL_HANDLE) {
        xrDestroySpace(head_space_);
        head_space_ = XR_NULL_HANDLE;
    }
    if (local_space_ != XR_NULL_HANDLE) {
        xrDestroySpace(local_space_);
        local_space_ = XR_NULL_HANDLE;
    }
    // Clear the C-linkage instance pointer so illixr_acquire_depth() no-ops
    // after shutdown.
    g_sensor_capture_instance = nullptr;

    // Only destroy the session and instance if we created them.
    // When reusing Unity's handles (owns_xr_ == false), Unity owns their lifetime.
    if (owns_xr_) {
        if (xr_session_ != XR_NULL_HANDLE) {
            xrDestroySession(xr_session_);
            xr_session_ = XR_NULL_HANDLE;
        }
        if (xr_instance_ != XR_NULL_HANDLE) {
            xrDestroyInstance(xr_instance_);
            xr_instance_ = XR_NULL_HANDLE;
        }
    } else {
        xr_session_  = XR_NULL_HANDLE;
        xr_instance_ = XR_NULL_HANDLE;
    }
}

// ---------------------------------------------------------------------------
// Vulkan depth readback - init/destroy
// ---------------------------------------------------------------------------

bool xr_sensor_capture::init_vulkan() {
    if (s_vk_interface_ == nullptr) {
        spdlog::get("illixr")->error("init_vulkan: IUnityGraphicsVulkan not available");
        return false;
    }

    // Get Unity's Vulkan instance struct (device, physicalDevice, queue, etc.)
    UnityVulkanInstance vk = s_vk_interface_->Instance();
    vk_device_             = vk.device;
    vk_physical_           = vk.physicalDevice;
    vk_queue_              = vk.graphicsQueue;
    vk_queue_family_       = vk.queueFamilyIndex;

    if (vk_device_ == VK_NULL_HANDLE) {
        spdlog::get("illixr")->error("init_vulkan: VkDevice is null");
        return false;
    }

    // Command pool
    VkCommandPoolCreateInfo pool_ci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool_ci.queueFamilyIndex = vk_queue_family_;
    pool_ci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(vk_device_, &pool_ci, nullptr, &vk_cmd_pool_) != VK_SUCCESS) {
        spdlog::get("illixr")->error("init_vulkan: vkCreateCommandPool failed");
        return false;
    }

    // Command buffer
    VkCommandBufferAllocateInfo alloc_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc_info.commandPool        = vk_cmd_pool_;
    alloc_info.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(vk_device_, &alloc_info, &vk_cmd_buf_) != VK_SUCCESS) {
        spdlog::get("illixr")->error("init_vulkan: vkAllocateCommandBuffers failed");
        return false;
    }

    // Fence (signalled = true so first wait doesn't block)
    VkFenceCreateInfo fence_ci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fence_ci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (vkCreateFence(vk_device_, &fence_ci, nullptr, &vk_fence_) != VK_SUCCESS) {
        spdlog::get("illixr")->error("init_vulkan: vkCreateFence failed");
        return false;
    }

    // Staging buffer - sized for depth frame, allocated lazily when dimensions known.
    // Actual allocation deferred to first acquire when depth_swapchain_width_ is set.
    spdlog::get("illixr")->info("init_vulkan: Vulkan readback resources created");
    return true;
}

void xr_sensor_capture::destroy_vulkan() {
    if (vk_device_ == VK_NULL_HANDLE)
        return;
    vkDeviceWaitIdle(vk_device_);
    if (vk_staging_buf_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(vk_device_, vk_staging_buf_, nullptr);
        vk_staging_buf_ = VK_NULL_HANDLE;
    }
    if (vk_staging_mem_ != VK_NULL_HANDLE) {
        vkFreeMemory(vk_device_, vk_staging_mem_, nullptr);
        vk_staging_mem_ = VK_NULL_HANDLE;
    }
    if (vk_fence_ != VK_NULL_HANDLE) {
        vkDestroyFence(vk_device_, vk_fence_, nullptr);
        vk_fence_ = VK_NULL_HANDLE;
    }
    if (vk_cmd_pool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(vk_device_, vk_cmd_pool_, nullptr);
        vk_cmd_pool_ = VK_NULL_HANDLE;
    }
    vk_device_ = VK_NULL_HANDLE;
}

uint32_t xr_sensor_capture::find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags props) const {
    VkPhysicalDeviceMemoryProperties mem_props{};
    vkGetPhysicalDeviceMemoryProperties(vk_physical_, &mem_props);
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
        if ((type_filter & (1u << i)) && (mem_props.memoryTypes[i].propertyFlags & props) == props)
            return i;
    }
    spdlog::get("illixr")->error("find_memory_type: no suitable memory type found");
    return UINT32_MAX;
}

// ---------------------------------------------------------------------------
// Pose lookup (RGB only - depth pose comes from XrEnvironmentDepthImageMETA)
// ---------------------------------------------------------------------------

bool xr_sensor_capture::get_pose_at_timestamp(XrTime timestamp, float out_matrix[16]) const {
    if (timestamp <= 0)
        return false;

    XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
    XrResult        result = xrLocateSpace(head_space_, local_space_, timestamp, &location);
    if (XR_FAILED(result)) {
        spdlog::get("illixr")->warn("xrLocateSpace failed: {}", static_cast<int>(result));
        return false;
    }

    const XrSpaceLocationFlags required = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    if ((location.locationFlags & required) != required) {
        spdlog::get("illixr")->warn("xrLocateSpace pose not valid (flags=0x{})", static_cast<unsigned>(location.locationFlags));
        return false;
    }

    spdlog::get("illixr")->debug("[xrLocateSpace] pose: {}, {}, {}, {}; {}, {}, {}", location.pose.orientation.x,
    location.pose.orientation.y,
    location.pose.orientation.z,
    location.pose.orientation.w,
    location.pose.position.x,
    location.pose.position.y,
    location.pose.position.z);
    const float x  = location.pose.orientation.x;
    const float y  = location.pose.orientation.y;
    const float z  = location.pose.orientation.z;
    const float w  = location.pose.orientation.w;
    const float tx = location.pose.position.x;
    const float ty = location.pose.position.y;
    const float tz = location.pose.position.z;

    out_matrix[0] = 1.f - 2.f * (y * y + z * z);
    out_matrix[1] = 2.f * (x * y - w * z);
    out_matrix[2] = 2.f * (x * z + w * y);
    out_matrix[3] = tx;

    out_matrix[4] = 2.f * (x * y + w * z);
    out_matrix[5] = 1.f - 2.f * (x * x + z * z);
    out_matrix[6] = 2.f * (y * z - w * x);
    out_matrix[7] = ty;

    out_matrix[8]  = 2.f * (x * z - w * y);
    out_matrix[9]  = 2.f * (y * z + w * x);
    out_matrix[10] = 1.f - 2.f * (x * x + y * y);
    out_matrix[11] = tz;

    out_matrix[12] = 0.f;
    out_matrix[13] = 0.f;
    out_matrix[14] = 0.f;
    out_matrix[15] = 1.f;

    return true;
}

// ---------------------------------------------------------------------------
// Depth acquisition via XR_META_environment_depth
//
// acquire_depth_unity_thread() is called from Unity's Update() via
// illixr_acquire_depth() in ILLIXRBridge. It runs on Unity's main thread
// while Unity's XR frame is open (between Unity's xrBeginFrame/xrEndFrame),
// which is the only valid window for xrAcquireEnvironmentDepthImageMETA.
// Unity's GL context is current on this thread so no separate EGL context
// is needed. The result is stored in depth_buffer_ under depth_mutex_ and
// consumed by the threadloop via timestamp matching.
// ---------------------------------------------------------------------------

void xr_sensor_capture::acquire_depth_unity_thread() {
    if (!depth_ext_available_ || depth_provider_ == XR_NULL_HANDLE)
        return;

    // Lazy swapchain init - deferred to here so Unity's session is running.
    if (depth_swapchain_ == XR_NULL_HANDLE) {
        XrEnvironmentDepthSwapchainCreateInfoMETA sc_ci{XR_TYPE_ENVIRONMENT_DEPTH_SWAPCHAIN_CREATE_INFO_META};
        sc_ci.createFlags = 0;
        XrResult result   = xr_create_depth_swapchain_(depth_provider_, &sc_ci, &depth_swapchain_);
        if (XR_FAILED(result)) {
            spdlog::get("illixr")->error("xrCreateEnvironmentDepthSwapchainMETA failed: {}", static_cast<int>(result));
            return;
        }

        XrEnvironmentDepthSwapchainStateMETA state{XR_TYPE_ENVIRONMENT_DEPTH_SWAPCHAIN_STATE_META};
        xr_get_depth_state_(depth_swapchain_, &state);
        depth_swapchain_width_  = static_cast<int32_t>(state.width);
        depth_swapchain_height_ = static_cast<int32_t>(state.height);
        // Log the actual Vulkan format so we know what pixel format the
        // runtime delivers - this determines how to interpret the raw bytes.
        spdlog::get("illixr")->info("Depth swapchain: {}x{}", state.width, state.height);

        uint32_t img_count = 0;
        xr_enum_depth_images_(depth_swapchain_, 0, &img_count, nullptr);

        // Unity uses Vulkan - enumerate as VkImage handles.
        std::vector<XrSwapchainImageVulkanKHR> images(img_count, {XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
        xr_enum_depth_images_(depth_swapchain_, img_count, &img_count,
                              reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data()));

        depth_vk_images_.resize(img_count);
        for (uint32_t i = 0; i < img_count; ++i)
            depth_vk_images_[i] = images[i].image;

        spdlog::get("illixr")->info("Depth swapchain: {} VkImage slots", img_count);

        // Query the actual Vulkan format of the depth image so we know
        // how to interpret the raw bytes after readback.
        if (img_count > 0 && vk_device_ != VK_NULL_HANDLE) {
            VkImageCreateInfo dummy{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
            // vkGetImageMemoryRequirements2 doesn't give us format, but we
            // can infer it from VkPhysicalDeviceImageFormatProperties2.
            // Simpler: check all plausible R16 formats against the image.
            // The most direct path is to log the raw bytes of the first few
            // pixels and let the format be determined from the data shape.
            // For now, log the image handle so we know enumeration succeeded.
            spdlog::get("illixr")->info("Depth VkImage[0] = {:p} - check vk_format in acquire log",
                                        static_cast<void*>(depth_vk_images_[0]));
        }

        // Allocate staging buffer now that we know dimensions.
        // R16F = 2 bytes/pixel.
        vk_staging_size_ = static_cast<VkDeviceSize>(depth_swapchain_width_ * depth_swapchain_height_ * 2);

        VkBufferCreateInfo buf_ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        buf_ci.size        = vk_staging_size_;
        buf_ci.usage       = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        buf_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(vk_device_, &buf_ci, nullptr, &vk_staging_buf_);

        VkMemoryRequirements mem_req{};
        vkGetBufferMemoryRequirements(vk_device_, vk_staging_buf_, &mem_req);

        VkMemoryAllocateInfo alloc_info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        alloc_info.allocationSize  = mem_req.size;
        alloc_info.memoryTypeIndex = find_memory_type(
            mem_req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        vkAllocateMemory(vk_device_, &alloc_info, nullptr, &vk_staging_mem_);
        vkBindBufferMemory(vk_device_, vk_staging_buf_, vk_staging_mem_, 0);
        spdlog::get("illixr")->info("Depth staging buffer allocated: {} bytes", vk_staging_size_);
    }

    // Acquire. Must be between xrBeginFrame / xrEndFrame - Unity ensures this
    // since we are called from MonoBehaviour.LateUpdate() during Unity's frame.
    XrEnvironmentDepthImageAcquireInfoMETA acq_info{XR_TYPE_ENVIRONMENT_DEPTH_IMAGE_ACQUIRE_INFO_META};
    acq_info.space       = local_space_;
    acq_info.displayTime = clock_boottime_xr();

    XrEnvironmentDepthImageMETA depth_image{XR_TYPE_ENVIRONMENT_DEPTH_IMAGE_META};
    depth_image.views[0] = {XR_TYPE_ENVIRONMENT_DEPTH_IMAGE_VIEW_META};
    depth_image.views[1] = {XR_TYPE_ENVIRONMENT_DEPTH_IMAGE_VIEW_META};

    XrResult result = xr_acquire_depth_image_(depth_provider_, &acq_info, &depth_image);
    if (result == XR_ENVIRONMENT_DEPTH_NOT_AVAILABLE_META)
        return; // not an error - provider hasn't produced a frame yet
    if (XR_FAILED(result)) {
        // -37 (XR_ERROR_CALL_ORDER_INVALID) means we were called outside
        // xrBeginFrame/xrEndFrame - happens on some Unity frames (GC, loading,
        // etc.) where the XR frame is not open during LateUpdate. Silent skip.
        if (static_cast<int>(result) != -37) {
            spdlog::get("illixr")->warn("xrAcquireEnvironmentDepthImageMETA failed: {}", static_cast<int>(result));
        }
        return;
    }

    // Use left-eye view (index 0).
    const XrEnvironmentDepthImageViewMETA& view = depth_image.views[0];

    // FOV tangents - angleLeft and angleDown are negative.
    const float tan_left  = std::tan(view.fov.angleLeft);
    const float tan_right = std::tan(view.fov.angleRight);
    const float tan_top   = std::tan(view.fov.angleUp);
    const float tan_down  = std::tan(view.fov.angleDown);

    // Use cached swapchain dimensions - static after creation.
    const float w_f      = static_cast<float>(depth_swapchain_width_);
    const float h_f      = static_cast<float>(depth_swapchain_height_);
    const float abs_left = std::abs(tan_left);
    const float abs_top  = std::abs(tan_top);

    camera_intrinsics intr{};
    intr.fx     = w_f / (std::abs(tan_right) + abs_left);
    intr.fy     = h_f / (std::abs(tan_top) + std::abs(tan_down));
    intr.cx     = abs_left * intr.fx;
    intr.cy     = abs_top * intr.fy;
    intr.width  = depth_swapchain_width_;
    intr.height = depth_swapchain_height_;

    // Depth pose from acquire result - no xrLocateSpace needed.
    float       pose_mat[16]{};
    const float qx = view.pose.orientation.x;
    const float qy = view.pose.orientation.y;
    const float qz = view.pose.orientation.z;
    const float qw = view.pose.orientation.w;
    const float tx = view.pose.position.x;
    const float ty = view.pose.position.y;
    const float tz = view.pose.position.z;

    pose_mat[0]  = 1.f - 2.f * (qy * qy + qz * qz);
    pose_mat[1]  = 2.f * (qx * qy - qw * qz);
    pose_mat[2]  = 2.f * (qx * qz + qw * qy);
    pose_mat[3]  = tx;
    pose_mat[4]  = 2.f * (qx * qy + qw * qz);
    pose_mat[5]  = 1.f - 2.f * (qx * qx + qz * qz);
    pose_mat[6]  = 2.f * (qy * qz - qw * qx);
    pose_mat[7]  = ty;
    pose_mat[8]  = 2.f * (qx * qz - qw * qy);
    pose_mat[9]  = 2.f * (qy * qz + qw * qx);
    pose_mat[10] = 1.f - 2.f * (qx * qx + qy * qy);
    pose_mat[11] = tz;
    pose_mat[12] = 0.f;
    pose_mat[13] = 0.f;
    pose_mat[14] = 0.f;
    pose_mat[15] = 1.f;

    // Store readback parameters for submit_depth_readback() which runs on
    // the render thread via GL.IssuePluginEvent(EVENT_ACQUIRE). The Vulkan
    // queue submit must happen on the render thread where Unity exclusively
    // owns vk_queue_ -- submitting from the main thread causes failures.
    needs_depth_release_ = true;

    {
        std::lock_guard<std::mutex> lock(pending_readback_mutex_);
        pending_readback_.image      = depth_vk_images_[depth_image.swapchainIndex];
        pending_readback_.width      = intr.width;
        pending_readback_.height     = intr.height;
        pending_readback_.intrinsics = intr;
        pending_readback_.near_z     = depth_image.nearZ;
        pending_readback_.far_z      = depth_image.farZ;
        pending_readback_.timestamp  = acq_info.displayTime;
        std::memcpy(pending_readback_.pose, pose_mat, sizeof(pose_mat));
        pending_readback_.valid = true;
    }
}

void xr_sensor_capture::submit_depth_readback() {
    pending_readback rb;
    {
        std::lock_guard<std::mutex> lock(pending_readback_mutex_);
        if (!pending_readback_.valid)
            return;
        rb                      = pending_readback_;
        pending_readback_.valid = false;
    }

    if (vk_staging_buf_ == VK_NULL_HANDLE) {
        spdlog::get("illixr")->error("submit_depth_readback: staging buffer not allocated");
        return;
    }

    const VkImage src_image = rb.image;
    const int32_t w         = rb.width;
    const int32_t h         = rb.height;

    // Wait for previous submission to finish, then reset fence and cmd buffer.
    vkWaitForFences(vk_device_, 1, &vk_fence_, VK_TRUE, UINT64_MAX);
    vkResetFences(vk_device_, 1, &vk_fence_);
    vkResetCommandBuffer(vk_cmd_buf_, 0);

    // Record copy commands.
    VkCommandBufferBeginInfo begin_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(vk_cmd_buf_, &begin_info);

    // Transition: current layout -> TRANSFER_SRC_OPTIMAL.
    // OpenXR external images from XR_META_environment_depth are delivered in
    // VK_IMAGE_LAYOUT_GENERAL (the OpenXR spec requires external images to
    // support GENERAL layout). Using SHADER_READ_ONLY_OPTIMAL as oldLayout
    // produces incorrect results when the actual layout is GENERAL.
    // srcAccessMask covers both shader reads and color attachment writes since
    // we don't know exactly which pipeline stage last touched the image.
    VkImageMemoryBarrier barrier_to_src{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier_to_src.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    barrier_to_src.dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
    barrier_to_src.oldLayout           = VK_IMAGE_LAYOUT_GENERAL;
    barrier_to_src.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier_to_src.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier_to_src.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier_to_src.image               = src_image;
    barrier_to_src.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(vk_cmd_buf_, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &barrier_to_src);

    // Copy image to buffer - R16F row-major, top-down.
    VkBufferImageCopy copy_region{};
    copy_region.bufferOffset      = 0;
    copy_region.bufferRowLength   = 0; // tightly packed
    copy_region.bufferImageHeight = 0;
    copy_region.imageSubresource  = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy_region.imageOffset       = {0, 0, 0};
    copy_region.imageExtent       = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
    vkCmdCopyImageToBuffer(vk_cmd_buf_, src_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vk_staging_buf_, 1, &copy_region);

    // Transition back: TRANSFER_SRC_OPTIMAL -> GENERAL
    // Return to GENERAL so the OpenXR runtime can use it again.
    VkImageMemoryBarrier barrier_to_read{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier_to_read.srcAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
    barrier_to_read.dstAccessMask       = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    barrier_to_read.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier_to_read.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
    barrier_to_read.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier_to_read.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier_to_read.image               = src_image;
    barrier_to_read.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(vk_cmd_buf_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &barrier_to_read);

    vkEndCommandBuffer(vk_cmd_buf_);

    // Submit and wait.
    VkSubmitInfo submit_info{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers    = &vk_cmd_buf_;
    if (vkQueueSubmit(vk_queue_, 1, &submit_info, vk_fence_) != VK_SUCCESS) {
        spdlog::get("illixr")->error("vkQueueSubmit for depth readback failed");
        // Re-signal the fence so the next call's wait doesn't block forever.
        vkResetFences(vk_device_, 1, &vk_fence_);
        VkFenceCreateInfo fence_ci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fence_ci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        vkDestroyFence(vk_device_, vk_fence_, nullptr);
        vkCreateFence(vk_device_, &fence_ci, nullptr, &vk_fence_);
        return;
    }
    vkWaitForFences(vk_device_, 1, &vk_fence_, VK_TRUE, UINT64_MAX);

    // Map staging buffer and copy to CPU vector - already top-down from Vulkan.
    void* mapped = nullptr;
    vkMapMemory(vk_device_, vk_staging_mem_, 0, vk_staging_size_, 0, &mapped);
    vkUnmapMemory(vk_device_, vk_staging_mem_);

    // Flip vertically — the Vulkan image is bottom-up from the OpenXR
    // depth provider, matching OpenGL convention. The server expects
    // top-down to match the RGB image orientation.
    std::vector<uint8_t> r16(static_cast<size_t>(w * h * 2));
    const auto*          src       = static_cast<const uint8_t*>(mapped);
    const size_t         row_bytes = static_cast<size_t>(w * 2); // 2 bytes per R16F pixel
    for (int r = 0; r < h; ++r) {
        const uint8_t* src_row = src + (h - 1 - r) * row_bytes;
        uint8_t*       dst_row = r16.data() + r * row_bytes;
        std::memcpy(dst_row, src_row, row_bytes);
    }
    vkUnmapMemory(vk_device_, vk_staging_mem_);

    // Log the first 4 pixels as raw uint16 and float16 so we can identify
    // the actual format from the data. Logged once then suppressed.
    static bool format_logged = false;
    if (!format_logged && r16.size() >= 8) {
        format_logged       = true;
        const uint16_t* u16 = reinterpret_cast<const uint16_t*>(r16.data());
        // Interpret as both uint16 and float16 so the format is identifiable.
        // R16_SFLOAT:  values ~0.0-1.0 when cast to float16
        // R16_UNORM:   values 0-65535 as uint16, divide by 65535 for [0,1]
        // R16_SNORM:   values -32768-32767, divide by 32767 for [-1,1]
        spdlog::get("illixr")->info("Depth pixel format probe - raw uint16: [{}, {}, {}, {}] "
                                    "nearZ={} farZ={}",
                                    u16[0], u16[1], u16[2], u16[3], rb.near_z, rb.far_z);
    }

    // Throttle: only store every DEPTH_ACQUIRE_EVERY LateUpdate ticks.
    // This gives ~10fps depth at 72Hz Unity, providing a spread of
    // timestamps to match against 2fps RGB frames.
    {
        std::lock_guard<std::mutex> lock(depth_mutex_);

        if (rb.near_z <= 0.f) {
            spdlog::get("illixr")->warn("submit_depth_readback: nearZ={} is zero or negative", rb.near_z);
        }

        depth_acquire_counter_++;
        if (depth_acquire_counter_ >= DEPTH_ACQUIRE_EVERY) {
            depth_acquire_counter_ = 0;
            depth_frame_data& slot = depth_cache_[depth_cache_next_];
            slot.data              = std::move(r16);
            slot.intrinsics        = rb.intrinsics;
            slot.near_z            = rb.near_z;
            slot.far_z             = rb.far_z;
            slot.timestamp         = rb.timestamp;
            std::memcpy(slot.pose, rb.pose, sizeof(rb.pose));
            slot.valid        = true;
            depth_cache_next_ = (depth_cache_next_ + 1) % DEPTH_CACHE_SIZE;
            spdlog::get("illixr")->debug("[depth] cached slot={} ts={} near_z={:.3f}",
                                         (depth_cache_next_ + DEPTH_CACHE_SIZE - 1) % DEPTH_CACHE_SIZE, rb.timestamp,
                                         rb.near_z);
        }
    }
}

const xr_sensor_capture::depth_frame_data* xr_sensor_capture::find_closest_depth(XrTime rgb_ts) const {
    // Called with depth_mutex_ held.
    const depth_frame_data* best    = nullptr;
    int64_t                 best_dt = INT64_MAX;
    for (const auto& entry : depth_cache_) {
        if (!entry.valid)
            continue;
        int64_t dt = std::abs(static_cast<int64_t>(rgb_ts) - static_cast<int64_t>(entry.timestamp));
        if (dt < best_dt) {
            best_dt = dt;
            best    = &entry;
        }
    }
    return best;
}

// C linkage so ILLIXRBridge can call this from Unity's Update() thread.
extern "C" void illixr_acquire_depth() {
    if (g_sensor_capture_instance != nullptr)
        g_sensor_capture_instance->acquire_depth_unity_thread();
}

// ---------------------------------------------------------------------------
// Camera2
// ---------------------------------------------------------------------------

static void on_camera_disconnected(void*, ACameraDevice* dev) {
    __android_log_print(ANDROID_LOG_ERROR, "xr_sensor_capture", "Camera disconnected: %p", static_cast<void*>(dev));
}

static void on_camera_error(void*, ACameraDevice* dev, int error) {
    __android_log_print(ANDROID_LOG_ERROR, "xr_sensor_capture", "Camera error %d on %p", error, static_cast<void*>(dev));
}

static void on_session_active(void*, ACameraCaptureSession*) {
    spdlog::get("illixr")->info("Capture session active");
}

static void on_session_ready(void*, ACameraCaptureSession*) {
    spdlog::get("illixr")->info("Capture session ready");
}

static void on_session_closed(void*, ACameraCaptureSession*) {
    spdlog::get("illixr")->info("Capture session closed");
}

bool xr_sensor_capture::init_camera() {
    camera_mgr_ = ACameraManager_create();
    if (camera_mgr_ == nullptr) {
        spdlog::get("illixr")->error("ACameraManager_create failed");
        return false;
    }

    ACameraIdList* id_list = nullptr;
    if (ACameraManager_getCameraIdList(camera_mgr_, &id_list) != ACAMERA_OK || id_list == nullptr || id_list->numCameras == 0) {
        spdlog::get("illixr")->error("No cameras found");
        return false;
    }

    // Quest 3 passthrough cameras report as back-facing with non-standard IDs.
    // Camera '1' is front-facing (logical/virtual), '50' and '51' are the
    // physical left and right RGB passthrough cameras.
    const char* selected_id = id_list->cameraIds[0]; // fallback
    for (int i = 0; i < id_list->numCameras; ++i) {
        if (std::strcmp(id_list->cameraIds[i], "51") == 0) {
            selected_id = id_list->cameraIds[i];
            break;
        }
    }
    spdlog::get("illixr")->info("Selected camera id='{}'", selected_id);

    if (selected_id == nullptr) {
        spdlog::get("illixr")->warn("No back-facing camera; falling back to camera 0");
        selected_id = id_list->cameraIds[2];
    }
    // Log all cameras and their facing values for diagnosis
    for (int i = 0; i < id_list->numCameras; ++i) {
        ACameraMetadata* meta = nullptr;
        ACameraManager_getCameraCharacteristics(camera_mgr_, id_list->cameraIds[i], &meta);
        ACameraMetadata_const_entry entry{};
        ACameraMetadata_getConstEntry(meta, ACAMERA_LENS_FACING, &entry);
        spdlog::get("illixr")->info("Camera[{}] id='{}' facing={}", i, id_list->cameraIds[i], (int) entry.data.u8[0]);
        ACameraMetadata_free(meta);
    }

    // Cache intrinsics.
    ACameraMetadata* meta = nullptr;
    ACameraManager_getCameraCharacteristics(camera_mgr_, selected_id, &meta);
    if (meta != nullptr) {
        ACameraMetadata_const_entry intr{};
        if (ACameraMetadata_getConstEntry(meta, ACAMERA_LENS_INTRINSIC_CALIBRATION, &intr) == ACAMERA_OK && intr.count >= 5) {
            rgb_intrinsics_.fx    = intr.data.f[0];
            rgb_intrinsics_.fy    = intr.data.f[1];
            rgb_intrinsics_.cx    = intr.data.f[2];
            rgb_intrinsics_.cy    = intr.data.f[3];
            rgb_intrinsics_valid_ = (rgb_intrinsics_.fx > 0.f);
        }
        ACameraMetadata_free(meta);
    }
    rgb_intrinsics_.width  = encoder_->width_;
    rgb_intrinsics_.height = encoder_->height_;

    ACameraDevice_StateCallbacks device_cbs{};
    device_cbs.onDisconnected = on_camera_disconnected;
    device_cbs.onError        = on_camera_error;
    if (ACameraManager_openCamera(camera_mgr_, selected_id, &device_cbs, &camera_device_) != ACAMERA_OK) {
        spdlog::get("illixr")->error("ACameraManager_openCamera failed");
        ACameraManager_deleteCameraIdList(id_list);
        return false;
    }
    ACameraManager_deleteCameraIdList(id_list);

    ACaptureSessionOutputContainer_create(&session_output_container_);
    ACaptureSessionOutput_create(encoder_->get_window(), &session_output_);
    ACaptureSessionOutputContainer_add(session_output_container_, session_output_);
    ACameraOutputTarget_create(encoder_->get_window(), &camera_output_target_);

    // Log supported output sizes for the encoder surface format (PRIVATE/implementation-defined).
    // This helps diagnose encoder dimension mismatches - if enc_width_ x enc_height_ is not
    // in this list, Camera2 will silently crop/scale and the encoder may produce black frames.
    {
        ACameraMetadata* char_meta = nullptr;
        ACameraManager_getCameraCharacteristics(camera_mgr_, selected_id, &char_meta);
        if (char_meta != nullptr) {
            ACameraMetadata_const_entry sizes{};
            if (ACameraMetadata_getConstEntry(char_meta, ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, &sizes) ==
                ACAMERA_OK) {
                spdlog::get("illixr")->info("Camera2 supported output sizes (format/w/h/input):");
                for (uint32_t i = 0; i + 3 < sizes.count; i += 4) {
                    if (sizes.data.i32[i + 3] == 0) { // output streams only
                        spdlog::get("illixr")->info("  format=0x{:X} {}x{}", sizes.data.i32[i], sizes.data.i32[i + 1],
                                                    sizes.data.i32[i + 2]);
                    }
                }
            }
            ACameraMetadata_free(char_meta);
        }
    }

    ACameraDevice_createCaptureRequest(camera_device_, TEMPLATE_PREVIEW, &capture_request_);
    ACaptureRequest_addTarget(capture_request_, camera_output_target_);

    // Throttle Camera2 to the configured capture rate. Without this the sensor
    // runs at its maximum rate (~30fps) regardless of tick_interval_ns_.
    // Setting both min and max to capture_fps_ locks the frame rate.
    const int32_t fps_range[2] = {static_cast<int32_t>(encoder_->capture_fps_), static_cast<int32_t>(encoder_->capture_fps_)};
    ACaptureRequest_setEntry_i32(capture_request_, ACAMERA_CONTROL_AE_TARGET_FPS_RANGE, 2, fps_range);
    spdlog::get("illixr")->info("Camera2 FPS range set to [{}, {}]", fps_range[0], fps_range[1]);

    ACameraCaptureSession_stateCallbacks session_cbs{};
    session_cbs.onActive = on_session_active;
    session_cbs.onReady  = on_session_ready;
    session_cbs.onClosed = on_session_closed;

    if (ACameraDevice_createCaptureSession(camera_device_, session_output_container_, &session_cbs, &capture_session_) !=
        ACAMERA_OK) {
        spdlog::get("illixr")->error("ACameraDevice_createCaptureSession failed");
        return false;
    }

    ACameraCaptureSession_setRepeatingRequest(capture_session_, nullptr, 1, &capture_request_, nullptr);
    spdlog::get("illixr")->info("Camera2 -> encoder surface: {}x{}", encoder_->width_, encoder_->height_);
    return true;
}

void xr_sensor_capture::destroy_camera() {
    if (capture_session_ != nullptr) {
        ACameraCaptureSession_stopRepeating(capture_session_);
        ACameraCaptureSession_close(capture_session_);
        capture_session_ = nullptr;
    }
    if (capture_request_ != nullptr) {
        ACaptureRequest_free(capture_request_);
        capture_request_ = nullptr;
    }
    if (camera_output_target_ != nullptr) {
        ACameraOutputTarget_free(camera_output_target_);
        camera_output_target_ = nullptr;
    }
    if (session_output_ != nullptr) {
        ACaptureSessionOutput_free(session_output_);
        session_output_ = nullptr;
    }
    if (session_output_container_ != nullptr) {
        ACaptureSessionOutputContainer_free(session_output_container_);
        session_output_container_ = nullptr;
    }
    if (camera_device_ != nullptr) {
        ACameraDevice_close(camera_device_);
        camera_device_ = nullptr;
    }
    if (camera_mgr_ != nullptr) {
        ACameraManager_delete(camera_mgr_);
        camera_mgr_ = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Threadloop
// ---------------------------------------------------------------------------

threadloop::skip_option xr_sensor_capture::_p_should_skip() {
    if (init_failed_)
        return skip_option::skip_and_yield;
    if (clock_boottime_xr() - last_tick_ns_ < tick_interval_ns_)
        return skip_option::skip_and_yield;
    return skip_option::run;
}

void xr_sensor_capture::_p_one_iteration() {
    last_tick_ns_ = clock_boottime_xr();

    // ---- Drain encoder output (RGB) ----
    encoder_->drain_encoder_output(clock_offset_ns_);

    // ---- Publish one semantic_data per pending encoded RGB frame ----
    for (auto& rgb : encoder_->pending_frames_) {
        float rgb_matrix[16]{};
        if (!get_pose_at_timestamp(rgb.timestamp, rgb_matrix)) {
            spdlog::get("illixr")->warn("[frame={}] RGB pose lookup failed, dropping", frame_number_);
            frame_number_++;
            continue;
        }

        // Find the closest depth frame in the cache by timestamp.
        const depth_frame_data* depth_snap = nullptr;
        {
            std::lock_guard<std::mutex> lock(depth_mutex_);
            depth_snap = find_closest_depth(rgb.timestamp);
        }

        if (depth_snap == nullptr) {
            spdlog::get("illixr")->warn("[frame={}] No depth in cache yet, dropping", frame_number_);
            frame_number_++;
            continue;
        }

        const int64_t delta_ns = std::abs(static_cast<int64_t>(rgb.timestamp) - static_cast<int64_t>(depth_snap->timestamp));

        spdlog::get("illixr")->debug("[frame={}] closest depth delta={}ms", frame_number_, delta_ns / 1'000'000LL);

        semantic_frame frame{};
        frame.frame_number     = frame_number_++;
        frame.image            = std::move(rgb.encoded);
        frame.intrinsics       = rgb_intrinsics_;
        frame.rgb_timestamp_ns = static_cast<int64_t>(rgb.timestamp);
        frame.max_depth        = max_depth_m_;
        std::memcpy(frame.rgb_camera_pose, rgb_matrix, sizeof(rgb_matrix));

        frame.depth              = depth_snap->data;
        frame.depth_near_z       = depth_snap->near_z;
        frame.depth_intrinsics   = depth_snap->intrinsics;
        frame.depth_timestamp_ns = static_cast<int64_t>(depth_snap->timestamp);
        std::memcpy(frame.depth_pose, depth_snap->pose, sizeof(depth_snap->pose));

        spdlog::get("illixr")->info("[publish] frame={} image={}B depth={}B depth_delta={}ms near_z={:.3f}", frame.frame_number,
                                    frame.image.size(), frame.depth.size(), delta_ns / 1'000'000LL, frame.depth_near_z);

        writer_.put(writer_.allocate<semantic_frame>(std::move(frame)));
    }

    encoder_->pending_frames_.clear();
}

// ---------------------------------------------------------------------------
// Plugin registration
// ---------------------------------------------------------------------------

PLUGIN_MAIN(xr_sensor_capture)
