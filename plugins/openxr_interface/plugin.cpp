#ifdef USING_OPENXR
#    include "plugin.hpp"

#    include <algorithm>
#    include <array>
#    include <cmath>
#    include <spdlog/spdlog.h>
#    include <sstream>
#    include <vector>

#    ifdef COMBINED_ENCODING
#        include "illixr/data_format/vulkan_context.hpp"
#    endif

// const int log_interval = 300; // pose logging interval in frames
using namespace ILLIXR;
using namespace ILLIXR::data_format;

namespace {
// Resolved once in JNI_OnLoad, which runs with the classloader that was active when this
// library was loaded. Looking this up lazily from init_network_config_panel() instead risks a
// ClassNotFoundException: that runs on a native worker thread, whose attached classloader
// context can default to the system/bootstrap classloader rather than the app's own -- the same
// issue the original dialog-based NetworkConfigDialog lookup had to work around this same way.
jclass g_network_config_panel_class = nullptr;
// Same reasoning, for the connection log display's nested class; init_connection_log_display()
// runs on the render thread, which has the identical classloader concern.
jclass g_log_display_panel_class = nullptr;
} // namespace

#    ifdef ILLIXR_ENABLE_BOBA
constexpr float BOBA_PANEL_DISTANCE_METERS = 1.1F;
constexpr float BOBA_PANEL_WIDTH_METERS    = 1.2F;
#    endif

// Identity pose helper
static XrPosef identity_pose() {
    XrPosef pose       = {{0}};
    pose.orientation.w = 1.0f;
    return pose;
}

#    ifdef ILLIXR_ENABLE_BOBA
// Rotate a local-space panel offset into the current view orientation without
// pulling another math library into the Android OpenXR entry point.
static XrVector3f rotate_vector(const XrQuaternionf& q, const XrVector3f& v) {
    const XrVector3f t{2.0F * (q.y * v.z - q.z * v.y), 2.0F * (q.z * v.x - q.x * v.z), 2.0F * (q.x * v.y - q.y * v.x)};
    return {v.x + q.w * t.x + (q.y * t.z - q.z * t.y), v.y + q.w * t.y + (q.z * t.x - q.x * t.z),
            v.z + q.w * t.z + (q.x * t.y - q.y * t.x)};
}

// Capture the world-space anchor used by mono_panel when that mode is entered.
static XrPosef panel_pose_from_view(const XrPosef& view_pose) {
    XrPosef          pose   = view_pose;
    const XrVector3f offset = rotate_vector(view_pose.orientation, {0.0F, 0.0F, -BOBA_PANEL_DISTANCE_METERS});
    pose.position.x += offset.x;
    pose.position.y += offset.y;
    pose.position.z += offset.z;
    return pose;
}
#    endif

// A fixed pose, far outside any normal play area, used to hide a fingertip marker whose hand
// isn't currently tracked. Deliberately NOT achieved by omitting the marker's layer from a given
// frame's xrEndFrame submission: doing that (only including a marker once its hand becomes
// tracked, rather than always including it) is what produced the freeze this works around --
// some runtimes appear to dislike the *set* of composition layers changing shape between frames
// mid-session, even though a constant set (as when both hands are tracked from the first frame)
// works fine indefinitely. Keeping the layer count constant and just relocating an unused marker
// sidesteps that without depending on a full explanation of why the runtime reacts that way.
static XrPosef parked_marker_pose() {
    XrPosef pose = identity_pose();
    pose.position.y = -100.0f;
    return pose;
}

[[maybe_unused]] oxr_interface::oxr_interface(const std::string& name_, phonebook* pb_)
    : threadloop{name_, pb_}
    , switchboard_{phonebook_->lookup_impl<switchboard>()}
    , app_{switchboard_->get_android_app()}
    , clock_{phonebook_->lookup_impl<relative_clock>()}
#    ifdef ILLIXR_ENABLE_BOBA
    , stoplight_{phonebook_->lookup_impl<stoplight>()}
#    endif
    , frame_reader_{switchboard_->get_reader<dual_frames>("unity_rendered_frame")}
#    ifdef ILLIXR_ENABLE_BOBA
    , boba_client_control_reader_{switchboard_->get_reader<switchboard::event_wrapper<std::string>>("boba_client_control")}
#    endif
    , oxr_relay_{std::make_shared<oxr_relay>(name_, pb_)} {
    use_depth_ = switchboard_->get_env_bool("ILLIXR_USE_DEPTH_IMAGES");
#    ifndef ILLIXR_ENABLE_BOBA
    overscan_ = switchboard_->get_env_double("ILLIXR_OVERSCAN", 1.0);

    headset_width_  = static_cast<int>(headset_width_ * overscan_);
    headset_height_ = static_cast<int>(headset_height_ * overscan_);
#    endif

    init_xr();
    create_session();
    oxr_relay_->initialize(instance_, session_, local_space_, view_space_);
    create_swapchains();
    spdlog::get("illixr")->info("oxr_interface: Vulkan session ready");
    entry_point_ = true;
    switchboard_->schedule<message_type>(id_, "oxr_log_message", [&](const switchboard::ptr<const message_type>& datum, size_t) {
        log_callback(datum);
    });
}

void oxr_interface::log_callback(const switchboard::ptr<const message_type>& datum) {
    std::lock_guard<std::mutex> guard(log_mtx_);
    spdlog::get("illixr")->debug("[oxr] rx: {}", datum->message);
    log_messages_.push_back(datum->message);
}

void oxr_interface::_p_thread_setup() {
    renderer_ = std::make_unique<stereo_renderer>();
    if (!renderer_->initialize(vk_instance_, vk_physical_device_, vk_device_, vk_queue_, vk_queue_family_,
                               swapchains_[0].format)) {
        spdlog::get("illixr")->error("oxr_interface: Failed to initialize Vulkan renderer");
        return;
    }
    renderer_->set_crop_region(headset_width_, headset_height_,                            // Original
                               (headset_width_ + 31) & ~31, (headset_height_ + 31) & ~31); // Padded

#    ifdef COMBINED_ENCODING
    // Tell the renderer that color frames contain both eyes side-by-side.
    // render_eye() will sample the left half (u_offset=0.0) for eye 0 and
    // the right half (u_offset=0.5) for eye 1.
    renderer_->set_combined_encoding(true);
    spdlog::get("illixr")->info("oxr_interface: combined encoding mode enabled in renderer");
#    endif
    spdlog::get("illixr")->info("oxr_interface: Vulkan renderer ready on render thread");

    // Must happen here, not in start(): this is the render thread, a different one than start()
    // ran on, and the JNI env/global-ref'd Java object this sets up are only usable correctly
    // when driven from the thread that will actually call into them each frame (run_frame(),
    // which _p_one_iteration() calls, which only ever runs on this thread).
    init_connection_log_display();
}

void oxr_interface::start() {
    use_tcp_ = switchboard_->use_tcp();
    use_udp_ = switchboard_->use_udp();
    spdlog::get("illixr")->debug("[oxr startup] {}  {}", use_tcp_, use_udp_);

    // Gather network configuration from the user via an in-VR quad-layer panel before any other
    // plugin's start() runs -- in particular the TCP/UDP network backends, which now make their
    // actual connections from start() rather than their constructors. This relies on external
    // guarantees that oxr_interface::start() runs before every other plugin's start(), since all
    // plugins are constructed before any of them are started.
    //
    // Must run before threadloop::start(): that spawns the render thread which drives this same
    // session's ordinary xrWaitFrame/xrBeginFrame/xrEndFrame loop via _p_one_iteration(), and a
    // session's frame loop can only be driven by one thread at a time. Running the config loop
    // afterward would mean two threads both trying to drive it concurrently.
    init_network_config_panel();
    run_network_config_loop();
    destroy_network_config_panel();

    threadloop::start();
    oxr_relay_->start();
}

void oxr_interface::stop() {
    oxr_relay_->stop();
    threadloop::stop();
}

void oxr_interface::init_xr() {
    PFN_xrInitializeLoaderKHR init_loader;
    xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR", (PFN_xrVoidFunction*) &init_loader);
    if (init_loader) {
        XrLoaderInitInfoAndroidKHR init_info_android = {XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
        init_info_android.applicationVM              = app_->activity->vm;
        init_info_android.applicationContext         = app_->activity->clazz;
        init_loader((XrLoaderInitInfoBaseHeaderKHR*) &init_info_android);
    }

    // Check which extensions are available
    uint32_t extension_count = 0;
    xrEnumerateInstanceExtensionProperties(nullptr, 0, &extension_count, nullptr);
    std::vector<XrExtensionProperties> available_extensions(extension_count, {XR_TYPE_EXTENSION_PROPERTIES});
    xrEnumerateInstanceExtensionProperties(nullptr, extension_count, &extension_count, available_extensions.data());

    // Log available extensions and check for hand tracking
    spdlog::get("illixr")->debug("Available OpenXR extensions:");
    for (const auto& ext : available_extensions) {
        spdlog::get("illixr")->debug("  - {} (v{})", ext.extensionName, ext.extensionVersion);
        if (strcmp(ext.extensionName, XR_EXT_HAND_TRACKING_EXTENSION_NAME) == 0) {
            oxr_relay_->hand_tracking_supported_ = true;
        }
        if (strcmp(ext.extensionName, XR_EXT_HAND_INTERACTION_EXTENSION_NAME) == 0) {
            oxr_relay_->hand_interaction_supported_ = true;
            spdlog::get("illixr")->info("Hand interaction extension available");
        }
        if (strcmp(ext.extensionName, XR_KHR_COMPOSITION_LAYER_DEPTH_EXTENSION_NAME) == 0) {
            depth_extension_supported_ = true;
            spdlog::get("illixr")->info("Depth composition layer extension available");
        }
        if (strcmp(ext.extensionName, XR_FB_SPACE_WARP_EXTENSION_NAME) == 0) {
            spacewarp_supported_ = true;
            spdlog::get("illixr")->info("App Spacewarp extension available");
        }
    }

    // Build the list of extensions to enable
    std::vector<const char*> enabled_extensions = {
        XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
        XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME // ← replaces OpenGL ES
    };

    if (oxr_relay_->hand_tracking_supported_) {
        enabled_extensions.push_back(XR_EXT_HAND_TRACKING_EXTENSION_NAME);
        spdlog::get("illixr")->info("Hand tracking extension will be enabled");
    }
    if (oxr_relay_->hand_interaction_supported_) {
        enabled_extensions.push_back(XR_EXT_HAND_INTERACTION_EXTENSION_NAME);
        spdlog::get("illixr")->info("Hand interaction extension will be enabled");
    }
    if (depth_extension_supported_ && use_depth_) {
        enabled_extensions.push_back(XR_KHR_COMPOSITION_LAYER_DEPTH_EXTENSION_NAME);
        spdlog::get("illixr")->info("Depth composition layer extension will be enabled");
    }
    if (spacewarp_supported_) {
        enabled_extensions.push_back(XR_FB_SPACE_WARP_EXTENSION_NAME);
        spdlog::get("illixr")->info("App Spacewarp extension will be enabled");
    }

    XrInstanceCreateInfoAndroidKHR android_info = {XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
    android_info.applicationVM                  = app_->activity->vm;
    android_info.applicationActivity            = app_->activity->clazz;

    XrInstanceCreateInfo create_info  = {XR_TYPE_INSTANCE_CREATE_INFO};
    create_info.next                  = &android_info;
    create_info.enabledExtensionCount = static_cast<uint32_t>(enabled_extensions.size());
    create_info.enabledExtensionNames = enabled_extensions.data();
    strcpy(create_info.applicationInfo.applicationName, "ILLIXR_oxr");
    create_info.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;

    OXR(xrCreateInstance(&create_info, &instance_))

    XrSystemGetInfo sgi = {XR_TYPE_SYSTEM_GET_INFO};
    sgi.formFactor      = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    OXR(xrGetSystem(instance_, &sgi, &system_id_))

    XrSystemProperties                sp       = {XR_TYPE_SYSTEM_PROPERTIES};
    XrSystemHandTrackingPropertiesEXT ht_props = {XR_TYPE_SYSTEM_HAND_TRACKING_PROPERTIES_EXT};
    if (oxr_relay_->hand_tracking_supported_)
        sp.next = &ht_props;
    OXR(xrGetSystemProperties(instance_, system_id_, &sp))
    if (oxr_relay_->hand_tracking_supported_) {
        oxr_relay_->hand_tracking_supported_ = ht_props.supportsHandTracking;
    }
    spdlog::get("illixr")->info("XR System: {}", sp.systemName);
}

void oxr_interface::create_session() {
    // Resolve extension function pointers
    PFN_xrGetVulkanGraphicsRequirements2KHR xrGetVulkanGraphicsRequirements2KHR = nullptr;
    PFN_xrCreateVulkanInstanceKHR           xrCreateVulkanInstanceKHR           = nullptr;
    PFN_xrGetVulkanGraphicsDevice2KHR       xrGetVulkanGraphicsDevice2KHR       = nullptr;
    PFN_xrCreateVulkanDeviceKHR             xrCreateVulkanDeviceKHR             = nullptr;

    OXR(xrGetInstanceProcAddr(instance_, "xrGetVulkanGraphicsRequirements2KHR",
                              (PFN_xrVoidFunction*) &xrGetVulkanGraphicsRequirements2KHR))
    OXR(xrGetInstanceProcAddr(instance_, "xrCreateVulkanInstanceKHR", (PFN_xrVoidFunction*) &xrCreateVulkanInstanceKHR))
    OXR(xrGetInstanceProcAddr(instance_, "xrGetVulkanGraphicsDevice2KHR", (PFN_xrVoidFunction*) &xrGetVulkanGraphicsDevice2KHR))
    OXR(xrGetInstanceProcAddr(instance_, "xrCreateVulkanDeviceKHR", (PFN_xrVoidFunction*) &xrCreateVulkanDeviceKHR))

    // Check Vulkan requirements
    XrGraphicsRequirementsVulkan2KHR vk_reqs = {XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR};
    OXR(xrGetVulkanGraphicsRequirements2KHR(instance_, system_id_, &vk_reqs))
    spdlog::get("illixr")->info("Vulkan required: min {}.{}, max {}.{}", XR_VERSION_MAJOR(vk_reqs.minApiVersionSupported),
                                XR_VERSION_MINOR(vk_reqs.minApiVersionSupported),
                                XR_VERSION_MAJOR(vk_reqs.maxApiVersionSupported),
                                XR_VERSION_MINOR(vk_reqs.maxApiVersionSupported));

    // Create VkInstance via OpenXR
    // Additional instance extensions needed for AHardwareBuffer import.
    const char* instance_extensions[] = {
        VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME, VK_KHR_EXTERNAL_FENCE_CAPABILITIES_EXTENSION_NAME};

    VkApplicationInfo app_info{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app_info.pApplicationName = "ILLIXR";
    app_info.apiVersion       = VK_API_VERSION_1_1;

    VkInstanceCreateInfo vk_inst_ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    vk_inst_ci.pApplicationInfo        = &app_info;
    vk_inst_ci.enabledExtensionCount   = static_cast<uint32_t>(std::size(instance_extensions));
    vk_inst_ci.ppEnabledExtensionNames = instance_extensions;

    XrVulkanInstanceCreateInfoKHR xr_inst_ci{XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR};
    xr_inst_ci.systemId               = system_id_;
    xr_inst_ci.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
    xr_inst_ci.vulkanCreateInfo       = &vk_inst_ci;

    VkResult vk_result = VK_SUCCESS;
    OXR(xrCreateVulkanInstanceKHR(instance_, &xr_inst_ci, &vk_instance_, &vk_result))
    if (vk_result != VK_SUCCESS) {
        throw std::runtime_error("xrCreateVulkanInstanceKHR: VkInstance creation failed");
    }
    spdlog::get("illixr")->info("VkInstance created via OpenXR");

    // Select physical device required by OpenXR
    XrVulkanGraphicsDeviceGetInfoKHR dev_info{XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR};
    dev_info.systemId       = system_id_;
    dev_info.vulkanInstance = vk_instance_;
    OXR(xrGetVulkanGraphicsDevice2KHR(instance_, &dev_info, &vk_physical_device_))
    spdlog::get("illixr")->info("VkPhysicalDevice selected by OpenXR");

    // Find graphics queue family
    uint32_t qf_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(vk_physical_device_, &qf_count, nullptr);
    std::vector<VkQueueFamilyProperties> qf_props(qf_count);
    vkGetPhysicalDeviceQueueFamilyProperties(vk_physical_device_, &qf_count, qf_props.data());
    for (uint32_t i = 0; i < qf_count; i++) {
        if (qf_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            vk_queue_family_ = i;
            break;
        }
    }

    // Create VkDevice via OpenXR
    // Request the extensions needed for AHardwareBuffer import and YCbCr.
    const char* device_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                                       VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME,
                                       VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME,
                                       VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
                                       VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
                                       VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME,
                                       VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME,
                                       VK_KHR_BIND_MEMORY_2_EXTENSION_NAME,
                                       VK_KHR_MAINTENANCE1_EXTENSION_NAME};

    float                   queue_priority = 1.0f;
    VkDeviceQueueCreateInfo q_ci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    q_ci.queueFamilyIndex = vk_queue_family_;
    q_ci.queueCount       = 1;
    q_ci.pQueuePriorities = &queue_priority;

    // Enable YCbCr conversion feature.
    VkPhysicalDeviceSamplerYcbcrConversionFeatures ycbcr_features{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES};
    ycbcr_features.samplerYcbcrConversion = VK_TRUE;

    VkDeviceCreateInfo vk_dev_ci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    vk_dev_ci.pNext                   = &ycbcr_features;
    vk_dev_ci.queueCreateInfoCount    = 1;
    vk_dev_ci.pQueueCreateInfos       = &q_ci;
    vk_dev_ci.enabledExtensionCount   = static_cast<uint32_t>(std::size(device_extensions));
    vk_dev_ci.ppEnabledExtensionNames = device_extensions;

    XrVulkanDeviceCreateInfoKHR xr_dev_ci{XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR};
    xr_dev_ci.systemId               = system_id_;
    xr_dev_ci.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
    xr_dev_ci.vulkanPhysicalDevice   = vk_physical_device_;
    xr_dev_ci.vulkanCreateInfo       = &vk_dev_ci;

    OXR(xrCreateVulkanDeviceKHR(instance_, &xr_dev_ci, &vk_device_, &vk_result))
    if (vk_result != VK_SUCCESS) {
        throw std::runtime_error("xrCreateVulkanDeviceKHR: VkDevice creation failed");
    }
    vkGetDeviceQueue(vk_device_, vk_queue_family_, 0, &vk_queue_);
    spdlog::get("illixr")->info("VkDevice created via OpenXR, queue family {}", vk_queue_family_);

    // Create XrSession with Vulkan binding
    XrGraphicsBindingVulkan2KHR vk_binding{XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR};
    vk_binding.instance         = vk_instance_;
    vk_binding.physicalDevice   = vk_physical_device_;
    vk_binding.device           = vk_device_;
    vk_binding.queueFamilyIndex = vk_queue_family_;
    vk_binding.queueIndex       = 0;

    XrSessionCreateInfo sci{XR_TYPE_SESSION_CREATE_INFO};
    sci.next     = &vk_binding;
    sci.systemId = system_id_;
    OXR(xrCreateSession(instance_, &sci, &session_))
    spdlog::get("illixr")->info("XrSession created (Vulkan binding)");

    // Reference spaces
    uint32_t view_count = 0;
    OXR(xrEnumerateViewConfigurationViews(instance_, system_id_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &view_count,
                                          nullptr))
    view_configs_[0].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
    view_configs_[1].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
    OXR(xrEnumerateViewConfigurationViews(instance_, system_id_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &view_count,
                                          view_configs_))

    XrReferenceSpaceCreateInfo rsi{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    rsi.poseInReferenceSpace = identity_pose();
    rsi.referenceSpaceType   = XR_REFERENCE_SPACE_TYPE_LOCAL;
    OXR(xrCreateReferenceSpace(session_, &rsi, &local_space_))
    rsi.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    OXR(xrCreateReferenceSpace(session_, &rsi, &view_space_))

#    ifdef COMBINED_ENCODING
    set_context(vk_instance_, vk_physical_device_, vk_device_, vk_queue_, vk_queue_family_);
    spdlog::get("illixr")->info("oxr_interface: VulkanDeviceContext registered in phonebook");
#    endif
}

void oxr_interface::_p_one_iteration() {
    poll_events();
    run_frame();
}

oxr_interface::~oxr_interface() {
    oxr_relay_->destroy();
    // Destroy renderer before Vulkan device
    if (renderer_) {
        renderer_->wait_idle();
        renderer_->cleanup();
        renderer_.reset();
    }

    for (auto& swapchain : swapchains_) {
        if (swapchain.swapchain != XR_NULL_HANDLE) {
            xrDestroySwapchain(swapchain.swapchain);
        }
    }

    if (view_space_ != XR_NULL_HANDLE) {
        xrDestroySpace(view_space_);
    }
    if (local_space_ != XR_NULL_HANDLE) {
        xrDestroySpace(local_space_);
    }
    if (session_ != XR_NULL_HANDLE) {
        xrDestroySession(session_);
    }
    if (instance_ != XR_NULL_HANDLE) {
        xrDestroyInstance(instance_);
    }

    for (auto& dsc : depth_swapchains_) {
        if (dsc.swapchain != XR_NULL_HANDLE) {
            xrDestroySwapchain(dsc.swapchain);
        }
    }

    for (auto& mv_sc : mv_swapchains_) {
        if (mv_sc.swapchain != XR_NULL_HANDLE) {
            xrDestroySwapchain(mv_sc.swapchain);
            mv_sc.swapchain = XR_NULL_HANDLE;
        }
    }

    // Safety net: if the app exits while still waiting for a first valid frame, this is never
    // called from run_frame()'s transition edge, so network_config_swapchain_ and its Vulkan
    // resources (reused for the connection log display) would otherwise leak. Idempotent, so
    // this is a no-op if it already ran.
    destroy_connection_log_display();

    // Destroy Vulkan objects
    if (vk_device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(vk_device_);
        vkDestroyDevice(vk_device_, nullptr);
    }
    if (vk_instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(vk_instance_, nullptr);
    }
}

void oxr_interface::poll_events() {
    XrEventDataBuffer event = {XR_TYPE_EVENT_DATA_BUFFER};

    while (xrPollEvent(instance_, &event) == XR_SUCCESS) {
        if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            auto* stateEvent = reinterpret_cast<XrEventDataSessionStateChanged*>(&event);
            session_state_   = stateEvent->state;
            spdlog::get("illixr")->debug("Session state → {}", static_cast<int>(session_state_));

            if (session_state_ == XR_SESSION_STATE_READY) {
                XrSessionBeginInfo begin_info           = {XR_TYPE_SESSION_BEGIN_INFO};
                begin_info.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                xrBeginSession(session_, &begin_info);
                session_running_ = XR_TRUE;
                spdlog::get("illixr")->debug("Session started");
            } else if (session_state_ == XR_SESSION_STATE_STOPPING) {
                xrEndSession(session_);
                session_running_ = XR_FALSE;
            }
        }
        event.type = XR_TYPE_EVENT_DATA_BUFFER;
    }
}

void oxr_interface::run_frame() {
#    ifdef ILLIXR_ENABLE_BOBA
    const auto client_control = boba_client_control_reader_.get_ro_nullable();
    if (!client_shutdown_requested_ && client_control != nullptr && **client_control == "shutdown") {
        client_shutdown_requested_ = true;
        spdlog::get("illixr")->info("Boba host requested native Quest shutdown");
        ANativeActivity_finish(app_->activity);
        stoplight_->signal_should_stop();
        return;
    }

#    endif

    if (!session_running_)
        return;

    // Wait for frame
    // XrFrameWaitInfo wait_info = {XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState frame_state = {XR_TYPE_FRAME_STATE};
    xrWaitFrame(session_, nullptr, &frame_state);

    // Begin frame
    XrFrameBeginInfo begin_info = {XR_TYPE_FRAME_BEGIN_INFO};
    xrBeginFrame(session_, &begin_info);

    XrCompositionLayerProjection projectionLayer = {XR_TYPE_COMPOSITION_LAYER_PROJECTION};
#    ifdef ILLIXR_ENABLE_BOBA
    XrCompositionLayerQuad panelLayer = {XR_TYPE_COMPOSITION_LAYER_QUAD};
#    endif
    XrCompositionLayerProjectionView projectionViews[2] = {{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},
                                                           {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
    XrCompositionLayerDepthInfoKHR   depth_infos[2]{{XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR},
                                                    {XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR}};

    // XrCompositionLayerSpaceWarpInfoFB structures — allocated on the
    // stack and chained onto projectionViews[eye].next when spacewarp is active.
    XrCompositionLayerSpaceWarpInfoFB spacewarp_infos[2]{{XR_TYPE_COMPOSITION_LAYER_SPACE_WARP_INFO_FB},
                                                         {XR_TYPE_COMPOSITION_LAYER_SPACE_WARP_INFO_FB}};

    // Used only before the first valid frame ever arrives; see the current_frames_ validity
    // check below. Reuses network_config_swapchain_/_quad_pose_/_quad_width_m_/_height_m_ --
    // the same quad the network config panel used -- rather than a full projection layer.
    XrCompositionLayerQuad log_quad_layer = {XR_TYPE_COMPOSITION_LAYER_QUAD};

    int                                 layer_count = 0;
    const XrCompositionLayerBaseHeader* layers[1]   = {nullptr};

    if (frame_state.shouldRender) {
        // Locate views (eye positions)
        XrViewState      view_state            = {XR_TYPE_VIEW_STATE};
        XrViewLocateInfo view_locate_info      = {XR_TYPE_VIEW_LOCATE_INFO};
        view_locate_info.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        view_locate_info.displayTime           = frame_state.predictedDisplayTime;
        oxr_relay_->update_time(frame_state.predictedDisplayTime);
        view_locate_info.space = local_space_;

        uint32_t view_count                = 2;
        views_[0].type                     = XR_TYPE_VIEW;
        views_[1].type                     = XR_TYPE_VIEW;
        const XrResult locate_views_result = xrLocateViews(session_, &view_locate_info, &view_state, 2, &view_count, views_);
        if (XR_SUCCEEDED(locate_views_result) && view_count == 2) {
#    ifdef ILLIXR_ENABLE_BOBA
            oxr_relay_->publish_boba_input(frame_state.predictedDisplayTime, frame_state.predictedDisplayPeriod,
                                           frame_state.shouldRender, view_state.viewStateFlags, views_, view_configs_);
#    endif

        } else {
            spdlog::get("illixr")->warn("xrLocateViews failed or returned {} views: {}", view_count,
                                        static_cast<int>(locate_views_result));
        }

        auto latest = frame_reader_.get_ro_nullable();
        if (latest != nullptr) {
            current_frames_ = latest; // current_frames_ is now ptr<const dual_frames>
            // int x = static_cast<int>(current_frames_->pose_id);
            spdlog::get("illixr")->debug("  Got new frame {}", current_frames_->frame_number);
            // Resolve the correlation
        }
        // current_frames_ retains last valid frame if nothing new arrived

        if (current_frames_ && current_frames_->is_valid()) {
            if (!first_valid_frame_received_) {
                first_valid_frame_received_ = true;
                destroy_connection_log_display(); // one-time: frees network_config_swapchain_
                                                  // and its Vulkan resources for good, now that
                                                  // real content is about to start rendering
            }
            // Import AHardwareBuffers into Vulkan (cached — no-op if buffer unchanged).
            renderer_->receive_frame(*current_frames_);

            // Look up the original pose measurement that was used to render this
            // frame so we can log the end-to-end pose tracking latency.
            {
                // static uint64_t pose_log_counter = 0;
                // const bool should_log_pose = (++pose_log_counter % 300) == 1;
                if (current_frames_->pose_id != 0) {
                    ILLIXR::pose_history_entry history_entry{};
                    if (oxr_relay_->get_pose_history(current_frames_->pose_id, history_entry)) {
                        // current time and XrTime for comparison
                        auto now_tp = time_point{std::chrono::duration<long, std::nano>{
                            std::chrono::high_resolution_clock::now().time_since_epoch()}};
                        auto now_xr = static_cast<int64_t>(frame_state.predictedDisplayTime);

                        // Age of the pose at display time
                        auto age_ns =
                            std::chrono::duration_cast<std::chrono::nanoseconds>(now_tp - history_entry.generated_time).count();
                        XrSpaceLocation loc = {XR_TYPE_SPACE_LOCATION};

                        OXR(xrLocateSpace(view_space_, local_space_, frame_state.predictedDisplayTime, &loc))
                        spdlog::get("illixr")->info(
                            "[pose_tracking] frame pose_id={} frame_id={} "
                            "initial_pose=({:.3f},{:.3f},{:.3f};{:.3f},{:.3f},{:.3f},{:.3f};{:.3f},{:.3f},{:.3f};{:.3f},{:.3f},"
                            "{:.3f}) "
                            "render_pose=({:.3f},{:.3f},{:.3f};{:.3f},{:.3f},{:.3f},{:.3f}) "
                            "currentPose=({:.3f},{:.3f},{:.3f};{:.3f},{:.3f},{:.3f},{:.3f}) "
                            "generated_xr_time={} "
                            "current_xr_time={} "
                            "xr_time_delta_ms={:.3f} "
                            "pose_age_ms={:.3f} "
                            "encode_time={:.3f}",
                            current_frames_->pose_id, current_frames_->frame_number, history_entry.pose.pose.position.x,
                            history_entry.pose.pose.position.y, history_entry.pose.pose.position.z,
                            history_entry.pose.pose.orientation.w, history_entry.pose.pose.orientation.x,
                            history_entry.pose.pose.orientation.y, history_entry.pose.pose.orientation.z,
                            history_entry.pose.linear_velocity.x, history_entry.pose.linear_velocity.y,
                            history_entry.pose.linear_velocity.z, history_entry.pose.angular_velocity.x,
                            history_entry.pose.angular_velocity.y, history_entry.pose.angular_velocity.z,
                            (current_frames_->pose[0].position.x + current_frames_->pose[1].position.x) / 2.,
                            (current_frames_->pose[0].position.y + current_frames_->pose[1].position.y) / 2.,
                            (current_frames_->pose[0].position.z + current_frames_->pose[1].position.z) / 2.,
                            current_frames_->pose[0].orientation.w, current_frames_->pose[0].orientation.x,
                            current_frames_->pose[0].orientation.y, current_frames_->pose[0].orientation.z, loc.pose.position.x,
                            loc.pose.position.y, loc.pose.position.z, loc.pose.orientation.w, loc.pose.orientation.x,
                            loc.pose.orientation.y, loc.pose.orientation.z, history_entry.xr_time, now_xr,
                            static_cast<double>(now_xr - static_cast<int64_t>(history_entry.xr_time)) / 1'000'000.0,
                            static_cast<double>(age_ns) / 1'000'000.0, current_frames_->encode_time);
                    } else {
                        spdlog::get("illixr")->debug("[pose_tracking] frame pose_id={} not found in history "
                                                     "(may have been pruned)",
                                                     current_frames_->pose_id);
                    }
                } else {
                    spdlog::get("illixr")->debug("[pose_tracker]  No current pose");
                }
            }
#    ifdef ILLIXR_ENABLE_BOBA
            const bool render_as_panel =
                current_frames_->presentation_mode != data_format::stereo_presentation_mode::stereo_fullscreen;
            if (current_frames_->presentation_mode == data_format::stereo_presentation_mode::mono_panel &&
                (previous_presentation_mode_ != data_format::stereo_presentation_mode::mono_panel ||
                 !world_panel_anchor_initialized_)) {
                world_panel_pose_               = panel_pose_from_view(views_[0].pose);
                world_panel_anchor_initialized_ = true;
            }
            previous_presentation_mode_ = current_frames_->presentation_mode;

            // Panel modes are monoscopic and use one compositor quad. Fullscreen mode renders both projection eyes.
            const int render_eye_count = render_as_panel ? 1 : 2;
#    else
            constexpr int render_eye_count = 2;
#    endif
            for (int eye = 0; eye < render_eye_count; eye++) {
                swapchain_info& sc = swapchains_[eye];

                uint32_t                    img_idx = 0;
                XrSwapchainImageAcquireInfo acq{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
                OXR(xrAcquireSwapchainImage(sc.swapchain, &acq, &img_idx))

                XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
                wait.timeout = XR_INFINITE_DURATION;
                OXR(xrWaitSwapchainImage(sc.swapchain, &wait))

                VkImage swapchain_vk_image = sc.images[img_idx].image;

                // Render color into swapchain VkImage.
                renderer_->render_eye(eye, swapchain_vk_image, sc.width, sc.height);

                XrSwapchainImageReleaseInfo rel{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                OXR(xrReleaseSwapchainImage(sc.swapchain, &rel))

#    ifdef ILLIXR_ENABLE_BOBA
                if (render_as_panel) {
                    panelLayer.layerFlags =
                        XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT | XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
                    panelLayer.space =
                        current_frames_->presentation_mode == data_format::stereo_presentation_mode::head_locked_panel
                        ? view_space_
                        : local_space_;
                    panelLayer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
                    panelLayer.pose =
                        current_frames_->presentation_mode == data_format::stereo_presentation_mode::head_locked_panel
                        ? identity_pose()
                        : world_panel_pose_;
                    if (current_frames_->presentation_mode == data_format::stereo_presentation_mode::head_locked_panel) {
                        panelLayer.pose.position.z = -BOBA_PANEL_DISTANCE_METERS;
                    }
                    const float aspect =
                        current_frames_->content_aspect_ratio > 0.0F ? current_frames_->content_aspect_ratio : 1.0F;
                    panelLayer.size                      = {BOBA_PANEL_WIDTH_METERS, BOBA_PANEL_WIDTH_METERS / aspect};
                    panelLayer.subImage.swapchain        = sc.swapchain;
                    panelLayer.subImage.imageRect.offset = {0, 0};
                    panelLayer.subImage.imageRect.extent = {static_cast<int32_t>(sc.width), static_cast<int32_t>(sc.height)};
                    panelLayer.subImage.imageArrayIndex  = 0;
                    continue;
                }
#    endif

                // Set up projection layer
                projectionViews[eye].pose = current_frames_->pose[eye];
                // Use the render FOV from the server if available (supports overdraw margins).
                // Fall back to the headset's native FOV if not yet received.
                if (current_frames_->fov_left[eye] != 0.0f) {
                    projectionViews[eye].fov.angleLeft  = current_frames_->fov_left[eye];
                    projectionViews[eye].fov.angleRight = current_frames_->fov_right[eye];
                    projectionViews[eye].fov.angleUp    = current_frames_->fov_up[eye];
                    projectionViews[eye].fov.angleDown  = current_frames_->fov_down[eye];
                } else {
                    projectionViews[eye].fov = views_[eye].fov;
                }
                projectionViews[eye].subImage.imageArrayIndex         = 0;
                projectionViews[eye].subImage.swapchain               = sc.swapchain;
                projectionViews[eye].subImage.imageRect.offset        = {0, 0};
                projectionViews[eye].subImage.imageRect.extent.width  = static_cast<int32_t>(sc.width);
                projectionViews[eye].subImage.imageRect.extent.height = static_cast<int32_t>(sc.height);

                // Depth swapchain (if enabled and frame has depth)

                if (use_depth_ && depth_extension_supported_ && current_frames_->has_valid_depth() &&
                    depth_swapchains_[eye].swapchain != XR_NULL_HANDLE) {
                    swapchain_info& dsc = depth_swapchains_[eye];

                    uint32_t depth_img_idx = 0;
                    OXR(xrAcquireSwapchainImage(dsc.swapchain, &acq, &depth_img_idx))
                    OXR(xrWaitSwapchainImage(dsc.swapchain, &wait))

                    VkImage depth_vk_image = dsc.images[depth_img_idx].image;
                    renderer_->render_eye_depth(eye, depth_vk_image, depth_format_, dsc.width, dsc.height);

                    OXR(xrReleaseSwapchainImage(dsc.swapchain, &rel))

                    // App Spacewarp layer
                    // Submitted when the frame carries both motion vectors and depth.
                    // The motion-vector and spacewarp-depth swapchains are at
                    // 432×432 (MV_SWAPCHAIN_WIDTH × MV_SWAPCHAIN_HEIGHT).
                    if (spacewarp_supported_ && current_frames_->has_valid_motion_vectors() &&
                        mv_swapchains_[eye].swapchain != XR_NULL_HANDLE) {
                        // Render motion vectors into the MV swapchain
                        swapchain_info& mv_sc      = mv_swapchains_[eye];
                        uint32_t        mv_img_idx = 0;
                        OXR(xrAcquireSwapchainImage(mv_sc.swapchain, &acq, &mv_img_idx))
                        OXR(xrWaitSwapchainImage(mv_sc.swapchain, &wait))

                        VkImage mv_vk_image = mv_sc.images[mv_img_idx].image;
                        renderer_->render_eye_motion_vec(eye, mv_vk_image, mv_sc.width, mv_sc.height);

                        OXR(xrReleaseSwapchainImage(mv_sc.swapchain, &rel))

                        // Build XrCompositionLayerSpaceWarpInfoFB
                        // appSpaceDeltaPose: relative transform of the app's tracking
                        // origin between the rendered frame and now.  For offload
                        // rendering with a fixed world origin this is identity.
                        XrPosef identity_delta{};
                        identity_delta.orientation.w = 1.0f;

                        XrCompositionLayerSpaceWarpInfoFB& sw = spacewarp_infos[eye];
                        sw.layerFlags                         = 0;

                        sw.motionVectorSubImage.swapchain        = mv_sc.swapchain;
                        sw.motionVectorSubImage.imageArrayIndex  = 0;
                        sw.motionVectorSubImage.imageRect.offset = {0, 0};
                        sw.motionVectorSubImage.imageRect.extent = {static_cast<int32_t>(mv_sc.width),
                                                                    static_cast<int32_t>(mv_sc.height)};

                        sw.appSpaceDeltaPose = identity_delta;

                        sw.depthSubImage.swapchain        = dsc.swapchain;
                        sw.depthSubImage.imageArrayIndex  = 0;
                        sw.depthSubImage.imageRect.offset = {0, 0};
                        sw.depthSubImage.imageRect.extent = {static_cast<int32_t>(dsc.width), static_cast<int32_t>(dsc.height)};

                        sw.minDepth = 0.0f;
                        sw.maxDepth = 1.0f;
                        sw.nearZ    = current_frames_->near_z;
                        sw.farZ     = current_frames_->far_z;

                        // Chain spacewarp info onto the projection view.
                        // If a depth_info was already chained, spacewarp takes
                        // precedence (it incorporates the same depth information).
                        projectionViews[eye].next = &spacewarp_infos[eye];
                    } else {
                        // Chain XrCompositionLayerDepthInfoKHR onto this projection view.
                        depth_infos[eye].subImage.swapchain        = dsc.swapchain;
                        depth_infos[eye].subImage.imageArrayIndex  = 0;
                        depth_infos[eye].subImage.imageRect.offset = {0, 0};
                        depth_infos[eye].subImage.imageRect.extent = {static_cast<int32_t>(dsc.width),
                                                                      static_cast<int32_t>(dsc.height)};
                        depth_infos[eye].minDepth                  = 0.0f;
                        depth_infos[eye].maxDepth                  = 1.0f;
                        depth_infos[eye].nearZ                     = current_frames_->near_z;
                        depth_infos[eye].farZ                      = current_frames_->far_z;
                        projectionViews[eye].next                  = &depth_infos[eye];
                    }
                }
            }

#    ifdef ILLIXR_ENABLE_BOBA
            if (render_as_panel) {
                layers[0] = reinterpret_cast<XrCompositionLayerBaseHeader*>(&panelLayer);
            } else
#    endif
            {
                projectionLayer.space      = local_space_;
                projectionLayer.viewCount  = 2;
                projectionLayer.views      = projectionViews;
                projectionLayer.layerFlags = XR_COMPOSITION_LAYER_CORRECT_CHROMATIC_ABERRATION_BIT |
                    XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT | XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
                layers[0] = reinterpret_cast<XrCompositionLayerBaseHeader*>(&projectionLayer);
            }
            layer_count = 1;
        } else if (!first_valid_frame_received_) {
            // Haven't received a single valid frame yet: show a black background with recent
            // connection-status log lines instead of leaving the compositor to freeze on
            // whatever was last displayed -- observed behavior when zero layers are submitted
            // (e.g. right after the network config panel's teardown, before this plugin has
            // ever rendered anything of its own). Reuses the network config panel's own quad
            // (pose, size, swapchain) rather than building a full projection layer: the config
            // panel phase itself only ever submitted quad layers, never a projection layer, and
            // never showed a "frozen background" problem -- so a quad-only submission appears
            // to composite correctly on this runtime, and reusing it avoids needing a second,
            // separately-sized Vulkan resource set just for this brief waiting period.
            render_connection_log_quad();

            log_quad_layer.space = local_space_;
            log_quad_layer.pose  = network_config_quad_pose_;
            log_quad_layer.size  = {network_config_quad_width_m_, network_config_quad_height_m_};
            log_quad_layer.subImage.swapchain               = network_config_swapchain_.swapchain;
            log_quad_layer.subImage.imageRect.offset        = {0, 0};
            log_quad_layer.subImage.imageRect.extent.width  = static_cast<int32_t>(network_config_swapchain_.width);
            log_quad_layer.subImage.imageRect.extent.height = static_cast<int32_t>(network_config_swapchain_.height);
            log_quad_layer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;

            layers[0]   = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&log_quad_layer);
            layer_count = 1;
        }
        // else: first_valid_frame_received_ is already true, but current_frames_ isn't valid
        // this particular frame (e.g. a transient bad frame arrived) -- matches the original,
        // pre-existing behavior of submitting nothing that frame, rather than either rendering
        // garbage frame data or flickering back to the log display.
    }
    // End frame (normally you'd submit layers here)
    XrFrameEndInfo endInfo       = {XR_TYPE_FRAME_END_INFO};
    endInfo.displayTime          = frame_state.predictedDisplayTime;
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.layerCount           = layer_count;
    endInfo.layers               = layers;
    auto    now_ns               = std::chrono::steady_clock::now().time_since_epoch().count();
    int64_t margin_ms            = (frame_state.predictedDisplayTime - now_ns) / 1'000'000;
    spdlog::get("illixr")->debug("xrEndFrame margin: {}ms", margin_ms);
    OXR(xrEndFrame(session_, &endInfo))

    frame_counter_++;
}

void oxr_interface::create_swapchains() {
    // Enumerate supported Vulkan swapchain formats and pick a suitable one.
    uint32_t fmt_count = 0;
    xrEnumerateSwapchainFormats(session_, 0, &fmt_count, nullptr);
    std::vector<int64_t> formats(fmt_count);
    xrEnumerateSwapchainFormats(session_, fmt_count, &fmt_count, formats.data());

#    ifdef ILLIXR_ENABLE_BOBA
    // Prefer an sRGB swapchain. The decoded video is converted back to linear
    // RGB in boba_color.frag before this attachment applies its output transfer.
    VkFormat chosen_fmt = formats.empty() ? VK_FORMAT_R8G8B8A8_UNORM : static_cast<VkFormat>(formats.front());
    const std::array<VkFormat, 4> preferred_formats{
        VK_FORMAT_R8G8B8A8_SRGB,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_B8G8R8A8_SRGB,
        VK_FORMAT_B8G8R8A8_UNORM,
    };
    for (const VkFormat preferred : preferred_formats) {
        if (std::find(formats.begin(), formats.end(), static_cast<int64_t>(preferred)) != formats.end()) {
            chosen_fmt = preferred;
            break;
        }
    }
#    else
    // Prefer R8G8B8A8_SRGB → R8G8B8A8_UNORM → B8G8R8A8_UNORM
    VkFormat chosen_fmt = VK_FORMAT_R8G8B8A8_UNORM;
    for (int64_t f : formats) {
        if (f == VK_FORMAT_R8G8B8A8_SRGB) {
            chosen_fmt = VK_FORMAT_R8G8B8A8_SRGB;
            break;
        }
        if (f == VK_FORMAT_R8G8B8A8_UNORM) {
            chosen_fmt = VK_FORMAT_R8G8B8A8_UNORM;
            break;
        }
        if (f == VK_FORMAT_B8G8R8A8_SRGB) {
            chosen_fmt = VK_FORMAT_B8G8R8A8_SRGB;
            break;
        }
    }
#    endif

    spdlog::get("illixr")->info("Swapchain format selected: 0x{:X}", static_cast<uint32_t>(chosen_fmt));

    // Create one swapchain per eye
    for (int eye = 0; eye < 2; eye++) {
        swapchain_info& sc = swapchains_[eye];

        sc.format = chosen_fmt;
        sc.width  = headset_width_;  // Your input resolution
        sc.height = headset_height_; // Your input resolution

        XrSwapchainCreateInfo swapchainInfo = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
        swapchainInfo.arraySize             = 1;
        swapchainInfo.format                = static_cast<int64_t>(chosen_fmt);
        swapchainInfo.width                 = sc.width;
        swapchainInfo.height                = sc.height;
        swapchainInfo.mipCount              = 1;
        swapchainInfo.faceCount             = 1;
        swapchainInfo.sampleCount           = 1;
        swapchainInfo.usageFlags            = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;

        OXR(xrCreateSwapchain(session_, &swapchainInfo, &sc.swapchain))

        // Enumerate swapchain images as Vulkan image handles.
        uint32_t img_count = 0;
        OXR(xrEnumerateSwapchainImages(sc.swapchain, 0, &img_count, nullptr))
        sc.images.resize(img_count, {XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR});
        OXR(xrEnumerateSwapchainImages(sc.swapchain, img_count, &img_count,
                                       reinterpret_cast<XrSwapchainImageBaseHeader*>(sc.images.data())))

        spdlog::get("illixr")->info("Eye {} swapchain: {}x{} format=0x{:X} images={}", eye, sc.width, sc.height,
                                    static_cast<uint32_t>(chosen_fmt), img_count);
    }

    if (use_depth_ && depth_extension_supported_) {
        // App Spacewarp swapchains
        // Both the motion-vector swapchain and the spacewarp depth swapchain are
        // created at 432×432, independent of the full-resolution color swapchains.
        if (spacewarp_supported_) {
            // Motion-vector swapchain — R16G16B16A16_SFLOAT
            // XR_SWAPCHAIN_USAGE_MUTABLE_FORMAT_BIT lets the runtime use an
            // aliased format internally if needed.
            for (int eye = 0; eye < 2; eye++) {
                swapchain_info& mv_sc = mv_swapchains_[eye];
                mv_sc.format          = mv_swapchain_format_;
                mv_sc.width           = MOTION_VEC_WIDTH;
                mv_sc.height          = MOTION_VEC_HEIGHT;

                XrSwapchainCreateInfo mv_info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
                mv_info.arraySize   = 1;
                mv_info.format      = static_cast<int64_t>(mv_swapchain_format_);
                mv_info.width       = MOTION_VEC_WIDTH;
                mv_info.height      = MOTION_VEC_HEIGHT;
                mv_info.mipCount    = 1;
                mv_info.faceCount   = 1;
                mv_info.sampleCount = 1;
                mv_info.usageFlags  = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT |
                    XR_SWAPCHAIN_USAGE_MUTABLE_FORMAT_BIT;

                OXR(xrCreateSwapchain(session_, &mv_info, &mv_sc.swapchain))

                uint32_t img_count = 0;
                OXR(xrEnumerateSwapchainImages(mv_sc.swapchain, 0, &img_count, nullptr))
                mv_sc.images.resize(img_count, {XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR});
                OXR(xrEnumerateSwapchainImages(mv_sc.swapchain, img_count, &img_count,
                                               reinterpret_cast<XrSwapchainImageBaseHeader*>(mv_sc.images.data())))

                spdlog::get("illixr")->info("Eye {} MV swapchain: {}x{} format=0x{:X} images={}", eye, MOTION_VEC_WIDTH,
                                            MOTION_VEC_HEIGHT, static_cast<uint32_t>(mv_swapchain_format_), img_count);
            }

            // Spacewarp depth swapchain — D32_SFLOAT (fallback D16_UNORM)
            depth_format_ = VK_FORMAT_D32_SFLOAT;
            for (int64_t f : formats) {
                if (f == VK_FORMAT_D32_SFLOAT) {
                    depth_format_ = VK_FORMAT_D32_SFLOAT;
                    break;
                }
                if (f == VK_FORMAT_D16_UNORM) {
                    depth_format_ = VK_FORMAT_D16_UNORM;
                    break;
                }
            }

            for (int eye = 0; eye < 2; eye++) {
                swapchain_info& sw_dsc = depth_swapchains_[eye];
                sw_dsc.format          = depth_format_;
                sw_dsc.width           = MOTION_VEC_WIDTH;
                sw_dsc.height          = MOTION_VEC_HEIGHT;

                XrSwapchainCreateInfo sw_depth_info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
                sw_depth_info.arraySize   = 1;
                sw_depth_info.format      = static_cast<int64_t>(depth_format_);
                sw_depth_info.width       = MOTION_VEC_WIDTH;
                sw_depth_info.height      = MOTION_VEC_HEIGHT;
                sw_depth_info.mipCount    = 1;
                sw_depth_info.faceCount   = 1;
                sw_depth_info.sampleCount = 1;
                sw_depth_info.usageFlags  = XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

                OXR(xrCreateSwapchain(session_, &sw_depth_info, &sw_dsc.swapchain))

                uint32_t img_count = 0;
                OXR(xrEnumerateSwapchainImages(sw_dsc.swapchain, 0, &img_count, nullptr))
                sw_dsc.images.resize(img_count, {XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR});
                OXR(xrEnumerateSwapchainImages(sw_dsc.swapchain, img_count, &img_count,
                                               reinterpret_cast<XrSwapchainImageBaseHeader*>(sw_dsc.images.data())))

                spdlog::get("illixr")->info("Eye {} spacewarp depth swapchain: {}x{} format=0x{:X} images={}", eye,
                                            MOTION_VEC_WIDTH, MOTION_VEC_HEIGHT, static_cast<uint32_t>(depth_format_),
                                            img_count);
            }
        } else {
            // Prefer D32_SFLOAT for maximum precision; fall back to D16_UNORM.
            depth_format_ = VK_FORMAT_D32_SFLOAT;
            for (int64_t f : formats) {
                if (f == VK_FORMAT_D16_UNORM) {
                    depth_format_ = VK_FORMAT_D16_UNORM;
                    break;
                }
                if (f == VK_FORMAT_D32_SFLOAT) {
                    depth_format_ = VK_FORMAT_D32_SFLOAT;
                    break;
                }
            }
            spdlog::get("illixr")->info("Depth swapchain format: 0x{:X}", static_cast<uint32_t>(depth_format_));

            for (int eye = 0; eye < 2; eye++) {
                swapchain_info& sc = depth_swapchains_[eye];
                sc.format          = depth_format_;
                sc.width           = headset_width_;
                sc.height          = headset_height_;

                XrSwapchainCreateInfo depth_info = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
                depth_info.arraySize             = 1;
                depth_info.format                = static_cast<int64_t>(depth_format_);
                depth_info.width                 = sc.width;
                depth_info.height                = sc.height;
                depth_info.mipCount              = 1;
                depth_info.faceCount             = 1;
                depth_info.sampleCount           = 1;
                depth_info.usageFlags            = XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

                OXR(xrCreateSwapchain(session_, &depth_info, &sc.swapchain))

                uint32_t img_count = 0;
                OXR(xrEnumerateSwapchainImages(sc.swapchain, 0, &img_count, nullptr))
                sc.images.resize(img_count, {XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR});
                OXR(xrEnumerateSwapchainImages(sc.swapchain, img_count, &img_count,
                                               reinterpret_cast<XrSwapchainImageBaseHeader*>(sc.images.data())))

                spdlog::get("illixr")->info("Eye {} depth swapchain: {}x{} format=0x{:X} images={}", eye, sc.width, sc.height,
                                            static_cast<uint32_t>(depth_format_), img_count);
            }
        }
    }
}


// =====================================================================================
// Network Config Panel
// =====================================================================================

void oxr_interface::init_network_config_panel() {
    JavaVM* vm = app_->activity->vm;
    if (vm->GetEnv(reinterpret_cast<void**>(&network_config_env_), JNI_VERSION_1_6) != JNI_OK) {
        vm->AttachCurrentThread(&network_config_env_, nullptr);
        network_config_did_attach_ = true;
    }
    JNIEnv* env = network_config_env_;

    if (g_network_config_panel_class == nullptr) {
        throw std::runtime_error(
            "oxr_interface: JNI_OnLoad did not cache NetworkConfigPanel's class -- either "
            "it wasn't called (check for a conflicting second JNI_OnLoad elsewhere in this "
            ".so; only one is allowed per shared library) or FindClass itself failed there "
            "(double check the class name/package/nesting)");
    }
    // Process-lifetime global ref cached by JNI_OnLoad; not owned per-instance, so no
    // NewGlobalRef here and no DeleteGlobalRef in destroy_network_config_panel().
    network_config_panel_class_ = g_network_config_panel_class;

    jmethodID ctor = env->GetMethodID(network_config_panel_class_, "<init>", "(Landroid/app/Activity;ZZ)V");

    // Confirms whether use_tcp_/use_udp_ are actually true here, in oxr_interface itself, before
    // anything JNI-related is even involved -- if this prints false/false (or wrong values),
    // the bug is upstream of this function entirely (wherever these get set relative to when
    // start() runs), not in the JNI call or the Java side.
    spdlog::get("illixr")->info("oxr_interface: constructing NetworkConfigPanel with use_tcp_={} use_udp_={}",
                                use_tcp_, use_udp_);

    // NewObject is variadic, exactly like CallXXXMethod: boolean args must be widened to jint,
    // per the same JNI-spec rule that bit us earlier in the dialog-based version.
    jobject local_obj = env->NewObject(network_config_panel_class_, ctor, app_->activity->clazz,
                                       static_cast<jint>(use_tcp_), static_cast<jint>(use_udp_));
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        throw std::runtime_error("oxr_interface: NetworkConfigPanel constructor threw a Java exception");
    }
    network_config_panel_ = env->NewGlobalRef(local_obj);
    env->DeleteLocalRef(local_obj);

    panel_render_method_        = env->GetMethodID(network_config_panel_class_, "render", "()Z");
    panel_get_bitmap_method_    = env->GetMethodID(network_config_panel_class_, "getBitmap", "()Landroid/graphics/Bitmap;");
    panel_handle_poke_method_   = env->GetMethodID(network_config_panel_class_, "handlePoke", "(FFZ)V");
    panel_update_hover_method_  = env->GetMethodID(network_config_panel_class_, "updateHover", "(IFFZ)V");
    panel_is_finished_method_   = env->GetMethodID(network_config_panel_class_, "isFinished", "()Z");
    panel_is_confirmed_method_  = env->GetMethodID(network_config_panel_class_, "isConfirmed", "()Z");
    panel_get_result_method_    = env->GetMethodID(network_config_panel_class_, "getResult", "()Ljava/lang/String;");
    panel_get_width_method_     = env->GetMethodID(network_config_panel_class_, "getPanelWidthPx", "()I");
    panel_get_height_method_    = env->GetMethodID(network_config_panel_class_, "getPanelHeightPx", "()I");

    auto panel_width  = static_cast<uint32_t>(env->CallIntMethod(network_config_panel_, panel_get_width_method_));
    auto panel_height = static_cast<uint32_t>(env->CallIntMethod(network_config_panel_, panel_get_height_method_));

    //  Quad swapchain, sized to match the panel bitmap
    network_config_swapchain_.width  = panel_width;
    network_config_swapchain_.height = panel_height;
    network_config_swapchain_.format = VK_FORMAT_R8G8B8A8_UNORM; // matches Bitmap.Config.ARGB_8888's actual byte layout (RGBA)

    XrSwapchainCreateInfo swapchain_info = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
    swapchain_info.arraySize   = 1;
    swapchain_info.format      = static_cast<int64_t>(network_config_swapchain_.format);
    swapchain_info.width       = panel_width;
    swapchain_info.height      = panel_height;
    swapchain_info.mipCount    = 1;
    swapchain_info.faceCount   = 1;
    swapchain_info.sampleCount = 1;
    // COLOR_ATTACHMENT_BIT is required here even though nothing renders into this image via a
    // render pass: upload_bitmap_to_quad_swapchain() ends by transitioning to
    // VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL (matching what the runtime expects for
    // composited color images, same as the eye swapchains), and that layout is only valid for
    // images actually created with this usage.
    swapchain_info.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT |
        XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    OXR(xrCreateSwapchain(session_, &swapchain_info, &network_config_swapchain_.swapchain))

    uint32_t image_count = 0;
    OXR(xrEnumerateSwapchainImages(network_config_swapchain_.swapchain, 0, &image_count, nullptr))
    network_config_swapchain_.images.resize(image_count, {XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR});
    OXR(xrEnumerateSwapchainImages(network_config_swapchain_.swapchain, image_count, &image_count,
                                   reinterpret_cast<XrSwapchainImageBaseHeader*>(network_config_swapchain_.images.data())))

    spdlog::get("illixr")->info("oxr_interface: network config quad swapchain {}x{}, images={}",
                                panel_width, panel_height, image_count);

    //  Small dedicated Vulkan resources for the bitmap upload (stereo_renderer_ doesn't exist
    //  yet -- it's created in _p_thread_setup(), on a different thread, after this constructor
    //  has already returned).
    VkCommandPoolCreateInfo pool_info = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool_info.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = vk_queue_family_;
    if (vkCreateCommandPool(vk_device_, &pool_info, nullptr, &network_config_cmd_pool_) != VK_SUCCESS) {
        throw std::runtime_error("oxr_interface: failed to create network config command pool");
    }

    VkCommandBufferAllocateInfo cmd_alloc_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cmd_alloc_info.commandPool        = network_config_cmd_pool_;
    cmd_alloc_info.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd_alloc_info.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(vk_device_, &cmd_alloc_info, &network_config_cmd_buffer_) != VK_SUCCESS) {
        throw std::runtime_error("oxr_interface: failed to allocate network config command buffer");
    }

    VkFenceCreateInfo fence_info = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (vkCreateFence(vk_device_, &fence_info, nullptr, &network_config_fence_) != VK_SUCCESS) {
        throw std::runtime_error("oxr_interface: failed to create network config fence");
    }

    network_config_staging_size_ = static_cast<VkDeviceSize>(panel_width) * panel_height * 4;

    VkBufferCreateInfo buffer_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer_info.size        = network_config_staging_size_;
    buffer_info.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(vk_device_, &buffer_info, nullptr, &network_config_staging_buf_) != VK_SUCCESS) {
        throw std::runtime_error("oxr_interface: failed to create network config staging buffer");
    }

    VkMemoryRequirements mem_reqs{};
    vkGetBufferMemoryRequirements(vk_device_, network_config_staging_buf_, &mem_reqs);

    VkPhysicalDeviceMemoryProperties mem_props{};
    vkGetPhysicalDeviceMemoryProperties(vk_physical_device_, &mem_props);

    uint32_t memory_type_index = UINT32_MAX;
    constexpr VkMemoryPropertyFlags kRequired =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        if ((mem_reqs.memoryTypeBits & (1u << i)) &&
            (mem_props.memoryTypes[i].propertyFlags & kRequired) == kRequired) {
            memory_type_index = i;
            break;
        }
    }
    if (memory_type_index == UINT32_MAX) {
        throw std::runtime_error("oxr_interface: no suitable host-visible memory type for staging buffer");
    }

    VkMemoryAllocateInfo alloc_info = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc_info.allocationSize  = mem_reqs.size;
    alloc_info.memoryTypeIndex = memory_type_index;
    if (vkAllocateMemory(vk_device_, &alloc_info, nullptr, &network_config_staging_mem_) != VK_SUCCESS) {
        throw std::runtime_error("oxr_interface: failed to allocate network config staging memory");
    }
    vkBindBufferMemory(vk_device_, network_config_staging_buf_, network_config_staging_mem_, 0);

    create_fingertip_markers();

    spdlog::get("illixr")->info("oxr_interface: network config panel ready");
}

void oxr_interface::initialize_quad_pose(XrTime time) {
    XrSpaceLocation view_loc = {XR_TYPE_SPACE_LOCATION};
    OXR(xrLocateSpace(view_space_, local_space_, time, &view_loc))

    const XrPosef view_pose = (view_loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)
        ? view_loc.pose
        : identity_pose();

    const XrQuaternionf& q = view_pose.orientation;
    // forward = R(q) * (0, 0, -1)
    const float fx = -2.0f * (q.x * q.z + q.w * q.y);
    const float fy = -2.0f * (q.y * q.z - q.w * q.x);
    const float fz = -(1.0f - 2.0f * (q.x * q.x + q.y * q.y));

    constexpr float kDistanceMeters = 0.5f; // was 0.6f; moved 10cm closer per feedback
    network_config_quad_pose_.orientation = view_pose.orientation;
    network_config_quad_pose_.position.x  = view_pose.position.x + fx * kDistanceMeters;
    network_config_quad_pose_.position.y  = view_pose.position.y + fy * kDistanceMeters;
    network_config_quad_pose_.position.z  = view_pose.position.z + fz * kDistanceMeters;
}

void oxr_interface::world_to_quad_local(float world_x, float world_y, float world_z, float& local_x, float& local_y,
                                        float& local_z) const {
    const float dx = world_x - network_config_quad_pose_.position.x;
    const float dy = world_y - network_config_quad_pose_.position.y;
    const float dz = world_z - network_config_quad_pose_.position.z;

    // Rotate (dx, dy, dz) into the quad's local frame by the conjugate of its orientation.
    const XrQuaternionf& q  = network_config_quad_pose_.orientation;
    const float           cx = -q.x;
    const float           cy = -q.y;
    const float           cz = -q.z;
    const float           cw = q.w;
    local_x = (1 - 2 * (cy * cy + cz * cz)) * dx + (2 * (cx * cy - cz * cw)) * dy + (2 * (cx * cz + cy * cw)) * dz;
    local_y = (2 * (cx * cy + cz * cw)) * dx + (1 - 2 * (cx * cx + cz * cz)) * dy + (2 * (cy * cz - cx * cw)) * dz;
    local_z = (2 * (cx * cz - cy * cw)) * dx + (2 * (cy * cz + cx * cw)) * dy + (1 - 2 * (cx * cx + cy * cy)) * dz;
}

void oxr_interface::update_network_config_input(XrTime predicted_display_time) {
    // Reuses oxr_relay_'s action set and poke_pose action spaces (accessible via `friend
    // oxr_interface` in oxr_relay.hpp) rather than a second, duplicate action set for the same
    // interaction profile -- see the comment on poke_engaged_ in the header for why.
    if (oxr_relay_->hand_interaction_action_set_ == XR_NULL_HANDLE) {
        return; // XR_EXT_hand_interaction not supported/initialized; no input possible for this panel
    }

    XrActiveActionSet active_set{oxr_relay_->hand_interaction_action_set_, XR_NULL_PATH};
    XrActionsSyncInfo sync_info = {XR_TYPE_ACTIONS_SYNC_INFO};
    sync_info.countActiveActionSets = 1;
    sync_info.activeActionSets      = &active_set;
    OXR(xrSyncActions(session_, &sync_info))

    const float half_width  = network_config_quad_width_m_ * 0.5f;
    const float half_height = network_config_quad_height_m_ * 0.5f;

    // Widened again from a prior round (0.20/0.35/0.45): logged data showed one genuine touch
    // reaching z~0.0 and another, elsewhere in the same session, sitting at z~0.2 without
    // registering -- a 20cm spread that's too large to be pure tracking jitter, and too large to
    // paper over by nudging the number slightly. This wider margin is a stopgap, not a resolved
    // calibration; if taps are still inconsistent, the more likely culprit is something other
    // than the threshold value itself (e.g. a within_xy/hit-rect issue for whichever specific
    // widget was being pressed), not something further threshold-widening will fix.
    //
    // Direction fixed from an earlier round: the quad's orientation is copied directly from
    // the view pose in initialize_quad_pose(), and OpenXR's view pose has -Z as the direction
    // the user is looking (into the distance, away from the user) -- so the quad's +Z, not -Z,
    // points back toward the user. "Close to the panel" is therefore small/near-zero local_z,
    // and "retreated far away" is large *positive* local_z -- the reverse of what the
    // comparisons below originally checked, which is why engagement almost never released once
    // triggered: retreating by pulling the hand back increases local_z, but the old disengage
    // condition required local_z to become more negative (i.e. pushing further through the
    // panel), which normal retreat never does.
    constexpr float kEngageDistanceMeters     = 0.02f;
    constexpr float kDisengageDistanceMeters  = 0.06f;
    constexpr float kCursorShowDistanceMeters = 0.55f;

    for (int hand = 0; hand < 2; hand++) {
        const XrSpace poke_space = oxr_relay_->interaction_pose_spaces_[hand][pose::POKE];
        if (poke_space == XR_NULL_HANDLE) {
            continue;
        }

        XrSpaceLocation loc = {XR_TYPE_SPACE_LOCATION};
        OXR(xrLocateSpace(poke_space, local_space_, predicted_display_time, &loc))
        if (!(loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)) {
            jvalue hover_args[4];
            hover_args[0].i = hand;
            hover_args[1].f = 0.0f;
            hover_args[2].f = 0.0f;
            hover_args[3].z = JNI_FALSE;
            network_config_env_->CallVoidMethodA(network_config_panel_, panel_update_hover_method_, hover_args);
            // Parked rather than simply "not visible": this hand's marker is still submitted
            // every frame (see run_network_config_loop()), just relocated somewhere unseen,
            // rather than removed from that frame's layer list.
            fingertip_marker_pose_[hand] = parked_marker_pose();
            continue;
        }

        // The fingertip depth marker uses the real, un-projected pose directly -- it's meant to
        // show the actual 3D gap between the finger and the panel, unlike the 2D cursor (baked
        // into the panel bitmap, and thus always flush with the panel's own depth).
        // Position tracks the real fingertip so depth is still meaningful; orientation
        // deliberately does NOT come from the poke pose's own rotation. The poke pose's
        // orientation follows the hand, and there are hand angles where that puts the marker
        // quad edge-on (or facing away) from the user, making it disappear. Using the panel's
        // own fixed orientation instead keeps the marker always facing the same way the panel
        // does -- behaving like a stable "pointer" reticle rather than a twisting flag.
        fingertip_marker_pose_[hand].position    = loc.pose.position;
        fingertip_marker_pose_[hand].orientation = network_config_quad_pose_.orientation;

        float local_x, local_y, local_z;
        world_to_quad_local(loc.pose.position.x, loc.pose.position.y, loc.pose.position.z, local_x, local_y, local_z);

        const bool within_xy      = (std::abs(local_x) <= half_width) && (std::abs(local_y) <= half_height);
        const bool near_plane     = within_xy && (local_z <= kEngageDistanceMeters);
        const bool far_from_plane = local_z > kDisengageDistanceMeters;

        //spdlog::get("illixr")->debug(
        //        "oxr_interface: poke hand={} local=({:.3f},{:.3f},{:.3f}) within_xy={} near={} far={} engaged={}",
        //        hand, local_x, local_y, local_z, within_xy, near_plane, far_from_plane, poke_engaged_[hand]);

        const float u = (local_x + half_width) / network_config_quad_width_m_;
        const float v = (half_height - local_y) / network_config_quad_height_m_; // OpenXR Y-up → bitmap Y-down

        // Cursor visibility uses a much more generous distance than the poke engage/disengage
        // thresholds, so the marker appears as the hand approaches rather than only at the
        // moment of a tap.
        const bool show_cursor = within_xy && (local_z <= kCursorShowDistanceMeters);

        {
            // Using CallVoidMethodA (jvalue array) rather than the variadic CallVoidMethod, for
            // the same reason as the poke call below: float/boolean arguments through a
            // variadic JNI call are subject to default-argument-promotion ambiguity that the
            // jvalue-array form sidesteps entirely.
            jvalue hover_args[4];
            hover_args[0].i = hand;
            hover_args[1].f = u;
            hover_args[2].f = v;
            hover_args[3].z = static_cast<jboolean>(show_cursor);
            network_config_env_->CallVoidMethodA(network_config_panel_, panel_update_hover_method_, hover_args);
        }

        if (!poke_engaged_[hand] && near_plane) {
            poke_engaged_[hand] = true;

            // Using CallVoidMethodA (jvalue array) rather than the variadic CallVoidMethod:
            // float arguments passed through a variadic JNI call are subject to the same kind
            // of default-argument-promotion ambiguity that bit the jboolean case earlier, and
            // the jvalue-array form sidesteps the question entirely rather than relying on
            // platform-specific promotion behavior.
            jvalue args[3];
            args[0].f = u;
            args[1].f = v;
            args[2].z = JNI_TRUE;
            network_config_env_->CallVoidMethodA(network_config_panel_, panel_handle_poke_method_, args);
        } else if (poke_engaged_[hand] && far_from_plane) {
            poke_engaged_[hand] = false;

            jvalue args[3];
            args[0].f = u;
            args[1].f = v;
            args[2].z = JNI_FALSE;
            network_config_env_->CallVoidMethodA(network_config_panel_, panel_handle_poke_method_, args);
        }

        // Pinch-select: click at this SAME (u, v) position -- the one the cursor is already
        // showing -- gated only by whether it's within the panel and by the pinch gesture
        // value, independent of how far the hand is from the panel. Deliberately does not use
        // a separate aim-ray pose for targeting (an earlier version did): that could land
        // somewhere different from what the yellow cursor showed, which is confusing since the
        // cursor is the only on-screen indication of where a pinch will actually act. "Pinch
        // clicks whatever the cursor is over" is the intended behavior, at any distance.
        //
        // kPinchValueIndex=0 is AIM's slot in oxr_relay_'s interaction_value_actions_ ordering
        // (see the Doxygen comment on interaction_pose_actions_ in oxr_relay.hpp: AIM=0, GRIP=1,
        // PINCH=2 for value/ready actions). Kept as AIM rather than switched to the PINCH slot
        // deliberately: AIM's value is already confirmed reliable from prior testing (isActive
        // consistently true, currentState cleanly reaching 0/1), and only the pose half of AIM
        // (the ray-cast target) is being dropped here, not the value half.
        constexpr int   kPinchValueIndex       = 0;
        constexpr float kPinchActivateThreshold = 0.5f;

        XrActionStateGetInfo pinch_value_info = {XR_TYPE_ACTION_STATE_GET_INFO};
        pinch_value_info.action        = oxr_relay_->interaction_value_actions_[kPinchValueIndex];
        pinch_value_info.subactionPath = oxr_relay_->hand_subaction_paths_[hand];

        XrActionStateFloat pinch_value_state = {XR_TYPE_ACTION_STATE_FLOAT};
        OXR(xrGetActionStateFloat(session_, &pinch_value_info, &pinch_value_state))

        const bool pinch_gesture_active = pinch_value_state.isActive &&
            pinch_value_state.currentState >= kPinchActivateThreshold;
        const bool pinch_down = within_xy && pinch_gesture_active;

        //spdlog::get("illixr")->debug(
        //        "oxr_interface: pinch hand={} within_xy={} currentState={:.3f} pinch_down={} engaged={}",
        //        hand, within_xy, pinch_value_state.currentState, pinch_down, pinch_engaged_[hand]);

        if (!pinch_engaged_[hand] && pinch_down) {
            pinch_engaged_[hand] = true;
            jvalue args[3];
            args[0].f = u;
            args[1].f = v;
            args[2].z = JNI_TRUE;
            network_config_env_->CallVoidMethodA(network_config_panel_, panel_handle_poke_method_, args);
        } else if (pinch_engaged_[hand] && !pinch_down) {
            pinch_engaged_[hand] = false;
            jvalue args[3];
            args[0].f = u;
            args[1].f = v;
            args[2].z = JNI_FALSE;
            network_config_env_->CallVoidMethodA(network_config_panel_, panel_handle_poke_method_, args);
        }
    }
}

void oxr_interface::upload_bitmap_to_quad_swapchain(jobject bitmap) {
    JNIEnv* env = network_config_env_;

    AndroidBitmapInfo info{};
    if (AndroidBitmap_getInfo(env, bitmap, &info) != ANDROID_BITMAP_RESULT_SUCCESS) {
        spdlog::get("illixr")->error("oxr_interface: AndroidBitmap_getInfo failed");
        return;
    }
    if (info.format != ANDROID_BITMAP_FORMAT_RGBA_8888) {
        spdlog::get("illixr")->error("oxr_interface: expected RGBA_8888 bitmap, got format {}",
                                     static_cast<int>(info.format));
        return;
    }

    void* src_pixels = nullptr;
    if (AndroidBitmap_lockPixels(env, bitmap, &src_pixels) != ANDROID_BITMAP_RESULT_SUCCESS) {
        spdlog::get("illixr")->error("oxr_interface: AndroidBitmap_lockPixels failed");
        return;
    }

    upload_pixels_to_swapchain_image(network_config_swapchain_, static_cast<uint8_t*>(src_pixels), info.stride,
                                     network_config_cmd_pool_, network_config_cmd_buffer_, network_config_fence_,
                                     network_config_staging_buf_, network_config_staging_mem_);

    AndroidBitmap_unlockPixels(env, bitmap);
}

void oxr_interface::upload_pixels_to_swapchain_image(swapchain_info& sc, const uint8_t* src_pixels,
                                                     uint32_t src_stride_bytes, VkCommandPool cmd_pool,
                                                     VkCommandBuffer cmd_buffer, VkFence fence, VkBuffer staging_buf,
                                                     VkDeviceMemory staging_mem) {
    (void) cmd_pool; // not directly used here (the buffer is already allocated from it), kept as
                     // a parameter so callers document/own which pool their buffer came from
    void* mapped = nullptr;
    vkMapMemory(vk_device_, staging_mem, 0, static_cast<VkDeviceSize>(sc.width) * sc.height * 4, 0, &mapped);
    // src_stride_bytes may exceed width*4 (row padding, e.g. from AndroidBitmap_getInfo); copy
    // row by row rather than assume tight packing between the source and the staging buffer.
    auto*          dst       = static_cast<uint8_t*>(mapped);
    const uint32_t row_bytes = sc.width * 4;
    for (uint32_t row = 0; row < sc.height; row++) {
        memcpy(dst + row * row_bytes, src_pixels + row * src_stride_bytes, row_bytes);
    }
    vkUnmapMemory(vk_device_, staging_mem);

    uint32_t                    image_index  = 0;
    XrSwapchainImageAcquireInfo acquire_info = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    OXR(xrAcquireSwapchainImage(sc.swapchain, &acquire_info, &image_index))

    XrSwapchainImageWaitInfo wait_info = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wait_info.timeout = XR_INFINITE_DURATION;
    OXR(xrWaitSwapchainImage(sc.swapchain, &wait_info))

    VkImage image = sc.images[image_index].image;

    vkResetCommandBuffer(cmd_buffer, 0);
    VkCommandBufferBeginInfo begin_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd_buffer, &begin_info);

    VkImageMemoryBarrier to_transfer{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    to_transfer.oldLayout            = VK_IMAGE_LAYOUT_UNDEFINED;
    to_transfer.newLayout            = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_transfer.srcQueueFamilyIndex  = VK_QUEUE_FAMILY_IGNORED;
    to_transfer.dstQueueFamilyIndex  = VK_QUEUE_FAMILY_IGNORED;
    to_transfer.image                = image;
    to_transfer.subresourceRange     = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    to_transfer.srcAccessMask        = 0;
    to_transfer.dstAccessMask        = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &to_transfer);

    VkBufferImageCopy region{};
    region.bufferOffset      = 0;
    region.bufferRowLength   = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource  = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageOffset       = {0, 0, 0};
    region.imageExtent       = {sc.width, sc.height, 1};
    vkCmdCopyBufferToImage(cmd_buffer, staging_buf, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    VkImageMemoryBarrier to_color{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    to_color.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_color.newLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; // matches the runtime's expectation for color swapchain images
    to_color.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_color.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_color.image               = image;
    to_color.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    to_color.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    to_color.dstAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    vkCmdPipelineBarrier(cmd_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &to_color);

    vkEndCommandBuffer(cmd_buffer);

    vkResetFences(vk_device_, 1, &fence);
    VkSubmitInfo submit_info{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers    = &cmd_buffer;
    vkQueueSubmit(vk_queue_, 1, &submit_info, fence);
    // Blocking on a fence per upload is simplicity over throughput: fine for the panel (a
    // handful of redraws while it's up) and acceptable for the log display (only runs until the
    // first valid frame arrives), but not something to reach for on stereo_renderer_'s own
    // steady-state per-frame path.
    vkWaitForFences(vk_device_, 1, &fence, VK_TRUE, UINT64_MAX);

    XrSwapchainImageReleaseInfo release_info = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    OXR(xrReleaseSwapchainImage(sc.swapchain, &release_info))
}

void oxr_interface::init_connection_log_display() {
    JavaVM* vm = app_->activity->vm;
    if (vm->GetEnv(reinterpret_cast<void**>(&render_thread_env_), JNI_VERSION_1_6) != JNI_OK) {
        vm->AttachCurrentThread(&render_thread_env_, nullptr);
        render_thread_did_attach_ = true;
    }
    JNIEnv* env = render_thread_env_;

    if (g_log_display_panel_class == nullptr) {
        spdlog::get("illixr")->error(
            "oxr_interface: JNI_OnLoad did not cache LogDisplayPanel's class; connection log "
            "display will not be available (real rendering is unaffected once frames arrive)");
        return;
    }
    if (network_config_swapchain_.swapchain == XR_NULL_HANDLE) {
        spdlog::get("illixr")->error(
            "oxr_interface: network_config_swapchain_ isn't available to reuse for the "
            "connection log display (was destroy_network_config_panel() changed to destroy "
            "it again?); connection log display will not be available");
        return;
    }

    jmethodID ctor = env->GetMethodID(g_log_display_panel_class, "<init>", "(Landroid/app/Activity;II)V");
    // NewObject is variadic: int args here are already a "safe" (non-narrower-than-int) type,
    // unlike the boolean case elsewhere in this file, so no special widening cast is needed.
    //
    // Sized to network_config_swapchain_'s resolution (the panel's, not the eye buffers') --
    // reusing that swapchain and its Vulkan upload resources (see below) means this must match
    // exactly, since the upload path does a direct row-by-row copy with no scaling.
    jobject local_obj = env->NewObject(g_log_display_panel_class, ctor, app_->activity->clazz,
                                       static_cast<jint>(network_config_swapchain_.width),
                                       static_cast<jint>(network_config_swapchain_.height));
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        spdlog::get("illixr")->error("oxr_interface: LogDisplayPanel constructor threw a Java exception");
        return;
    }
    log_display_panel_ = env->NewGlobalRef(local_obj);
    env->DeleteLocalRef(local_obj);

    log_panel_set_text_method_   = env->GetMethodID(g_log_display_panel_class, "setLogText", "(Ljava/lang/String;)V");
    log_panel_render_method_     = env->GetMethodID(g_log_display_panel_class, "render", "()Z");
    log_panel_get_bitmap_method_ = env->GetMethodID(g_log_display_panel_class, "getBitmap", "()Landroid/graphics/Bitmap;");

    // Deliberately no new Vulkan resources here: network_config_cmd_pool_/cmd_buffer_/fence_/
    // staging_buf_/staging_mem_ are still alive (destroy_network_config_panel() stopped
    // destroying them) and are reused directly, sized correctly already for
    // network_config_swapchain_'s resolution.

    spdlog::get("illixr")->info("oxr_interface: connection log display ready ({}x{}, reusing the network config quad)",
                                network_config_swapchain_.width, network_config_swapchain_.height);
}

void oxr_interface::render_connection_log_quad() {
    if (log_display_panel_ == nullptr) {
        return; // init_connection_log_display() failed; nothing to do (real rendering is unaffected)
    }
    JNIEnv* env = render_thread_env_;

    std::string joined;
    {
        std::lock_guard<std::mutex> lock(log_mtx_);
        constexpr size_t kMaxDisplayedLines = 40;
        const size_t     start = log_messages_.size() > kMaxDisplayedLines
            ? log_messages_.size() - kMaxDisplayedLines
            : 0;
        for (size_t i = start; i < log_messages_.size(); i++) {
            joined += log_messages_[i];
            joined += "\n";
        }
    }

    if (joined != last_sent_connection_log_) {
        jstring jtext = env->NewStringUTF(joined.c_str());
        env->CallVoidMethod(log_display_panel_, log_panel_set_text_method_, jtext);
        env->DeleteLocalRef(jtext);
        last_sent_connection_log_ = joined;
    }

    const jboolean redrew = env->CallBooleanMethod(log_display_panel_, log_panel_render_method_);
    if (!redrew) {
        return; // bitmap unchanged since the last frame; the quad's swapchain image already shows it
    }

    jobject bitmap = env->CallObjectMethod(log_display_panel_, log_panel_get_bitmap_method_);

    AndroidBitmapInfo info{};
    if (AndroidBitmap_getInfo(env, bitmap, &info) != ANDROID_BITMAP_RESULT_SUCCESS) {
        spdlog::get("illixr")->error("oxr_interface: AndroidBitmap_getInfo failed for log display");
        env->DeleteLocalRef(bitmap);
        return;
    }

    void* src_pixels = nullptr;
    if (AndroidBitmap_lockPixels(env, bitmap, &src_pixels) != ANDROID_BITMAP_RESULT_SUCCESS) {
        spdlog::get("illixr")->error("oxr_interface: AndroidBitmap_lockPixels failed for log display");
        env->DeleteLocalRef(bitmap);
        return;
    }

    // Single quad, reusing network_config_swapchain_ and its Vulkan upload resources -- see
    // destroy_network_config_panel()'s comment for why these are still alive at this point.
    upload_pixels_to_swapchain_image(network_config_swapchain_, static_cast<uint8_t*>(src_pixels), info.stride,
                                     network_config_cmd_pool_, network_config_cmd_buffer_, network_config_fence_,
                                     network_config_staging_buf_, network_config_staging_mem_);

    AndroidBitmap_unlockPixels(env, bitmap);
    env->DeleteLocalRef(bitmap);
}

void oxr_interface::create_fingertip_markers() {
    // A small, solid, semi-transparent-edged dot. Uploaded once per hand; only the marker's
    // pose changes thereafter (see update_network_config_input()), so there's no need to keep
    // regenerating or re-uploading this texture every frame.
    constexpr uint32_t kMarkerTexSize = 32;
    std::vector<uint8_t> dot_pixels(static_cast<size_t>(kMarkerTexSize) * kMarkerTexSize * 4, 0);
    const float center = (kMarkerTexSize - 1) / 2.0f;
    const float radius = kMarkerTexSize / 2.0f - 1.0f;
    for (uint32_t y = 0; y < kMarkerTexSize; y++) {
        for (uint32_t x = 0; x < kMarkerTexSize; x++) {
            const float dx = static_cast<float>(x) - center;
            const float dy = static_cast<float>(y) - center;
            uint8_t*    px = &dot_pixels[(static_cast<size_t>(y) * kMarkerTexSize + x) * 4];
            if (dx * dx + dy * dy <= radius * radius) {
                px[0] = 255;
                px[1] = 60;
                px[2] = 60;
                px[3] = 255; // solid red
            }
            // else left at (0,0,0,0): transparent
        }
    }

    for (int hand = 0; hand < 2; hand++) {
        swapchain_info& sc = fingertip_marker_swapchain_[hand];
        sc.width  = kMarkerTexSize;
        sc.height = kMarkerTexSize;
        sc.format = VK_FORMAT_R8G8B8A8_UNORM;

        XrSwapchainCreateInfo sci = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
        sci.arraySize   = 1;
        sci.format      = static_cast<int64_t>(sc.format);
        sci.width       = kMarkerTexSize;
        sci.height      = kMarkerTexSize;
        sci.mipCount    = 1;
        sci.faceCount   = 1;
        sci.sampleCount = 1;
        sci.usageFlags  = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT |
            XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
        OXR(xrCreateSwapchain(session_, &sci, &sc.swapchain))

        uint32_t image_count = 0;
        OXR(xrEnumerateSwapchainImages(sc.swapchain, 0, &image_count, nullptr))
        sc.images.resize(image_count, {XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR});
        OXR(xrEnumerateSwapchainImages(sc.swapchain, image_count, &image_count,
                                       reinterpret_cast<XrSwapchainImageBaseHeader*>(sc.images.data())))

        upload_pixels_to_swapchain_image(sc, dot_pixels.data(), kMarkerTexSize * 4,
                                         network_config_cmd_pool_, network_config_cmd_buffer_, network_config_fence_,
                                         network_config_staging_buf_, network_config_staging_mem_);
    }

    spdlog::get("illixr")->info("oxr_interface: fingertip depth markers ready");
}

void oxr_interface::run_network_config_loop() {
    // xrWaitFrame requires the session to actually be running.
    while (!session_running_) {
        poll_events();
    }

    bool quad_pose_initialized = false;

    // Defensive: if XR_EXT_hand_interaction isn't supported at all, update_network_config_input()
    // returns immediately without ever touching fingertip_marker_pose_, which would otherwise be
    // left at its default-constructed (invalid, all-zero-quaternion) state for the whole session
    // -- and these are now submitted every frame regardless of tracking state.
    fingertip_marker_pose_[0] = parked_marker_pose();
    fingertip_marker_pose_[1] = parked_marker_pose();

    while (network_config_env_->CallBooleanMethod(network_config_panel_, panel_is_finished_method_) == JNI_FALSE) {
        poll_events();

        XrFrameState frame_state = {XR_TYPE_FRAME_STATE};
        xrWaitFrame(session_, nullptr, &frame_state);
        xrBeginFrame(session_, nullptr);

        if (!quad_pose_initialized) {
            initialize_quad_pose(frame_state.predictedDisplayTime);
            quad_pose_initialized = true;
        }

        int                                  layer_count = 0;
        const XrCompositionLayerBaseHeader*   layers[3]   = {nullptr, nullptr, nullptr}; // panel + up to 2 fingertip markers
        XrCompositionLayerQuad                quad_layer  = {XR_TYPE_COMPOSITION_LAYER_QUAD};
        XrCompositionLayerQuad                marker_layers[2] = {{XR_TYPE_COMPOSITION_LAYER_QUAD},
                                                                {XR_TYPE_COMPOSITION_LAYER_QUAD}};

        if (frame_state.shouldRender) {
            update_network_config_input(frame_state.predictedDisplayTime);

            const jboolean redrew =
                network_config_env_->CallBooleanMethod(network_config_panel_, panel_render_method_);
            if (redrew) {
                jobject bitmap =
                    network_config_env_->CallObjectMethod(network_config_panel_, panel_get_bitmap_method_);
                upload_bitmap_to_quad_swapchain(bitmap);
                network_config_env_->DeleteLocalRef(bitmap);
            }

            quad_layer.space                            = local_space_;
            quad_layer.pose                              = network_config_quad_pose_;
            quad_layer.size                              = {network_config_quad_width_m_, network_config_quad_height_m_};
            quad_layer.subImage.swapchain                = network_config_swapchain_.swapchain;
            quad_layer.subImage.imageRect.offset         = {0, 0};
            quad_layer.subImage.imageRect.extent.width   = static_cast<int32_t>(network_config_swapchain_.width);
            quad_layer.subImage.imageRect.extent.height  = static_cast<int32_t>(network_config_swapchain_.height);
            quad_layer.layerFlags                        = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;

            layers[0]   = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quad_layer);
            layer_count = 1;

            constexpr float kMarkerSizeMeters = 0.015f; // was 0.02f; matches the panel's 25% reduction
            for (int hand = 0; hand < 2; hand++) {
                XrCompositionLayerQuad& ml = marker_layers[hand];
                ml.space                           = local_space_;
                ml.pose                            = fingertip_marker_pose_[hand]; // live pose if tracked, parked pose otherwise
                ml.size                            = {kMarkerSizeMeters, kMarkerSizeMeters};
                ml.subImage.swapchain               = fingertip_marker_swapchain_[hand].swapchain;
                ml.subImage.imageRect.offset        = {0, 0};
                ml.subImage.imageRect.extent.width  = static_cast<int32_t>(fingertip_marker_swapchain_[hand].width);
                ml.subImage.imageRect.extent.height = static_cast<int32_t>(fingertip_marker_swapchain_[hand].height);
                ml.layerFlags                       = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;

                layers[layer_count] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&ml);
                layer_count++;
            }
        }

        XrFrameEndInfo end_info = {XR_TYPE_FRAME_END_INFO};
        end_info.displayTime          = frame_state.predictedDisplayTime;
        end_info.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        end_info.layerCount           = layer_count;
        end_info.layers               = layers;
        OXR(xrEndFrame(session_, &end_info))
    }

    const auto    result_jstr = static_cast<jstring>(
        network_config_env_->CallObjectMethod(network_config_panel_, panel_get_result_method_));
    const jboolean confirmed = network_config_env_->CallBooleanMethod(network_config_panel_, panel_is_confirmed_method_);

    if (confirmed && result_jstr != nullptr) {
        const char* result_cstr = network_config_env_->GetStringUTFChars(result_jstr, nullptr);
        apply_network_config_result(std::string(result_cstr));
        network_config_env_->ReleaseStringUTFChars(result_jstr, result_cstr);
    } else {
        spdlog::get("illixr")->warn(
            "oxr_interface: network config panel cancelled; network backends will use "
            "whatever env vars/defaults were already set");
    }
    if (result_jstr != nullptr) {
        network_config_env_->DeleteLocalRef(result_jstr);
    }
}

void oxr_interface::apply_network_config_result(const std::string& result) {
    // Wire format, fields present only for active backends, always ending with client_ip:
    //   [tcp_server_ip|tcp_server_port|tcp_client_port|]
    //   [udp_server_ip|udp_server_port|udp_client_port|]
    //   client_ip
    // Mirrors NetworkConfigPanel.onConnect()'s wire format exactly.
    std::vector<std::string> fields;
    std::stringstream        ss(result);
    std::string              field;
    while (std::getline(ss, field, '|')) {
        fields.push_back(field);
    }

    const size_t expected_count = (use_tcp_ ? 3 : 0) + (use_udp_ ? 3 : 0) + 1;
    if (fields.size() != expected_count) {
        spdlog::get("illixr")->error(
            "oxr_interface: malformed network config result, expected {} fields, got {}",
            expected_count, fields.size());
        return;
    }

    size_t      idx = 0;
    std::string client_ip;
    if (use_tcp_) {
        switchboard_->set_env("ILLIXR_TCP_SERVER_IP", fields[idx++]);
        switchboard_->set_env("ILLIXR_TCP_SERVER_PORT", fields[idx++]);
        switchboard_->set_env("ILLIXR_TCP_CLIENT_PORT", fields[idx++]);
    }
    if (use_udp_) {
        switchboard_->set_env("ILLIXR_UDP_SERVER_IP", fields[idx++]);
        switchboard_->set_env("ILLIXR_UDP_SERVER_PORT", fields[idx++]);
        switchboard_->set_env("ILLIXR_UDP_CLIENT_PORT", fields[idx++]);
    }
    client_ip = fields[idx++];
    if (use_tcp_) {
        setenv("ILLIXR_TCP_CLIENT_IP", client_ip.c_str(), 1);
    }
    if (use_udp_) {
        setenv("ILLIXR_UDP_CLIENT_IP", client_ip.c_str(), 1);
    }

    spdlog::get("illixr")->info("oxr_interface: network config applied (client_ip={})", client_ip);
}

void oxr_interface::destroy_network_config_panel() {
    // network_config_swapchain_ and its Vulkan upload resources (cmd pool/buffer, fence,
    // staging buffer/memory) are deliberately NOT destroyed here anymore -- they're reused by
    // the connection log display (as a quad layer, same pose/size the config panel used) until
    // the first valid frame ever arrives, at which point destroy_connection_log_display() frees
    // them for good. Reusing them avoids needing a second, separately-sized Vulkan resource set
    // for the log display, and (per the config panel phase itself, which only ever submitted
    // quad layers and never showed a "frozen background" problem) submitting a quad with no
    // projection layer appears to composite correctly on this runtime, unlike submitting zero
    // layers at all, which is what caused the original freeze.

    for (auto& sc : fingertip_marker_swapchain_) {
        if (sc.swapchain != XR_NULL_HANDLE) {
            xrDestroySwapchain(sc.swapchain);
            sc.swapchain = XR_NULL_HANDLE;
        }
    }

    // No action set/space teardown here: poke input reads oxr_relay_'s own action set and
    // spaces, which oxr_relay_ owns and destroys itself.

    JNIEnv* env = network_config_env_;
    if (network_config_panel_ != nullptr) {
        env->DeleteGlobalRef(network_config_panel_);
        network_config_panel_ = nullptr;
    }
    // network_config_panel_class_ is NOT released here: it's g_network_config_panel_class,
    // cached once for the process's lifetime in JNI_OnLoad, not owned per-instance.
    network_config_panel_class_ = nullptr;
    if (network_config_did_attach_) {
        app_->activity->vm->DetachCurrentThread();
        network_config_did_attach_ = false;
    }
    network_config_env_ = nullptr;

    spdlog::get("illixr")->info("oxr_interface: network config panel torn down (quad swapchain kept alive for the connection log display)");
}

/// Final teardown of the (reused) quad swapchain and Vulkan upload resources, called the first
/// time a valid frame ever arrives (see run_frame()) -- or from the destructor, if the app exits
/// while still waiting for one. Idempotent (checks each handle before destroying and resets it
/// to NULL/VK_NULL_HANDLE afterward), so it's safe to call from both places without double-destroying.
void oxr_interface::destroy_connection_log_display() {
    if (network_config_fence_ != VK_NULL_HANDLE) {
        vkDestroyFence(vk_device_, network_config_fence_, nullptr);
        network_config_fence_ = VK_NULL_HANDLE;
    }
    if (network_config_cmd_pool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(vk_device_, network_config_cmd_pool_, nullptr); // also frees the allocated command buffer
        network_config_cmd_pool_   = VK_NULL_HANDLE;
        network_config_cmd_buffer_ = VK_NULL_HANDLE;
    }
    if (network_config_staging_buf_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(vk_device_, network_config_staging_buf_, nullptr);
        network_config_staging_buf_ = VK_NULL_HANDLE;
    }
    if (network_config_staging_mem_ != VK_NULL_HANDLE) {
        vkFreeMemory(vk_device_, network_config_staging_mem_, nullptr);
        network_config_staging_mem_ = VK_NULL_HANDLE;
    }
    if (network_config_swapchain_.swapchain != XR_NULL_HANDLE) {
        xrDestroySwapchain(network_config_swapchain_.swapchain);
        network_config_swapchain_.swapchain = XR_NULL_HANDLE;
    }
    if (log_display_panel_ != nullptr && render_thread_env_ != nullptr) {
        render_thread_env_->DeleteGlobalRef(log_display_panel_);
        log_display_panel_ = nullptr;
    }
    if (render_thread_did_attach_) {
        app_->activity->vm->DetachCurrentThread();
        render_thread_did_attach_ = false;
    }
    render_thread_env_ = nullptr;

    spdlog::get("illixr")->info("oxr_interface: connection log display torn down (first valid frame received)");
}

extern "C" plugin* this_plugin_factory(phonebook* pb) {
    auto* obj = new oxr_interface("openxr_interface", pb);
    // The runtime owns the plugin returned by this factory. Register a non-owning
    // service alias so the phonebook does not try to delete the same object again.
    pb->register_impl<vk::vulkan_context_provider>(std::shared_ptr<vk::vulkan_context_provider>(
        static_cast<vk::vulkan_context_provider*>(obj), [](vk::vulkan_context_provider*) { }));
    return obj;
}

// NOTE: only one JNI_OnLoad is permitted per shared library. If another translation unit
// linked into this same .so already defines one (e.g. a leftover from the earlier
// dialog-based approach, if that file is still part of the build), this will be a duplicate
// symbol at link time -- move this caching logic into that existing JNI_OnLoad instead of
// keeping both.
extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR;
    }

    jclass local_class = env->FindClass("com/example/ILLIXR/ILLIXRNativeActivity$NetworkConfigPanel");
    if (local_class == nullptr) {
        return JNI_ERR;
    }
    g_network_config_panel_class = static_cast<jclass>(env->NewGlobalRef(local_class));
    env->DeleteLocalRef(local_class);

    // Soft-fail only: if this specific lookup fails (e.g. LogDisplayPanel hasn't been added to
    // the build yet), leave g_log_display_panel_class null rather than returning JNI_ERR here --
    // init_connection_log_display() already checks for null and degrades gracefully (no log
    // display, but real rendering once frames arrive is unaffected). Returning JNI_ERR from
    // JNI_OnLoad itself would risk the JVM unloading the whole library, which would break the
    // already-working NetworkConfigPanel caching above too.
    jclass log_display_local_class =
            env->FindClass("com/example/ILLIXR/ILLIXRNativeActivity$LogDisplayPanel");
    if (log_display_local_class != nullptr) {
        g_log_display_panel_class = static_cast<jclass>(env->NewGlobalRef(log_display_local_class));
        env->DeleteLocalRef(log_display_local_class);
    } else {
        env->ExceptionClear(); // FindClass throws on failure; clear it so it doesn't leak into later JNI calls
    }

    return JNI_VERSION_1_6;
}
#endif
