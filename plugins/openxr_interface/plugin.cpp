#ifdef USING_OPENXR
#    include "plugin.hpp"

#    include <algorithm>
#    include <array>
#    include <spdlog/spdlog.h>
#    include <vector>

#    ifdef COMBINED_ENCODING
#        include "illixr/data_format/vulkan_context.hpp"
#    endif

// const int log_interval = 300; // pose logging interval in frames
using namespace ILLIXR;
using namespace ILLIXR::data_format;

constexpr int   I_HEADSET_WIDTH            = NATIVE_STREAM_EYE_WIDTH;
constexpr int   I_HEADSET_HEIGHT           = NATIVE_STREAM_EYE_HEIGHT;
constexpr float BOBA_PANEL_DISTANCE_METERS = 1.1F;
constexpr float BOBA_PANEL_WIDTH_METERS    = 1.2F;

// Identity pose helper
static XrPosef identity_pose() {
    XrPosef pose       = {{0}};
    pose.orientation.w = 1.0f;
    return pose;
}

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

[[maybe_unused]] oxr_interface::oxr_interface(const std::string& name_, phonebook* pb_)
    : threadloop{name_, pb_}
    , switchboard_{phonebook_->lookup_impl<switchboard>()}
    , app_{switchboard_->get_android_app()}
    , clock_{phonebook_->lookup_impl<relative_clock>()}
    , stoplight_{phonebook_->lookup_impl<stoplight>()}
    , frame_reader_{switchboard_->get_reader<dual_frames>("unity_rendered_frame")}
    , boba_client_control_reader_{switchboard_->get_reader<switchboard::event_wrapper<std::string>>("boba_client_control")}
    , oxr_relay_{std::make_shared<oxr_relay>(name_, pb_)} {
    use_depth_ = switchboard_->get_env_bool("ILLIXR_USE_DEPTH_IMAGES");
    init_xr();
    create_session();
    oxr_relay_->initialize(instance_, session_, local_space_, view_space_);
    create_swapchains();
    spdlog::get("illixr")->info("oxr_interface: Vulkan session ready");
}

void oxr_interface::_p_thread_setup() {
    renderer_ = std::make_unique<stereo_renderer>();
    if (!renderer_->initialize(vk_instance_, vk_physical_device_, vk_device_, vk_queue_, vk_queue_family_,
                               swapchains_[0].format)) {
        spdlog::get("illixr")->error("oxr_interface: Failed to initialize Vulkan renderer");
        return;
    }
    renderer_->set_crop_region(I_HEADSET_WIDTH, I_HEADSET_HEIGHT,                            // Original
                               (I_HEADSET_WIDTH + 31) & ~31, (I_HEADSET_HEIGHT + 31) & ~31); // Padded

#    ifdef COMBINED_ENCODING
    // Tell the renderer that color frames contain both eyes side-by-side.
    // render_eye() will sample the left half (u_offset=0.0) for eye 0 and
    // the right half (u_offset=0.5) for eye 1.
    renderer_->set_combined_encoding(true);
    spdlog::get("illixr")->info("oxr_interface: combined encoding mode enabled in renderer");
#    endif
    spdlog::get("illixr")->info("oxr_interface: Vulkan renderer ready on render thread");
}

void oxr_interface::start() {
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
    const auto client_control = boba_client_control_reader_.get_ro_nullable();
    if (!client_shutdown_requested_ && client_control != nullptr && **client_control == "shutdown") {
        client_shutdown_requested_ = true;
        spdlog::get("illixr")->info("Boba host requested native Quest shutdown");
        ANativeActivity_finish(app_->activity);
        stoplight_->signal_should_stop();
        return;
    }

    if (!session_running_)
        return;

    // Wait for frame
    // XrFrameWaitInfo wait_info = {XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState frame_state = {XR_TYPE_FRAME_STATE};
    xrWaitFrame(session_, nullptr, &frame_state);

    // Begin frame
    XrFrameBeginInfo begin_info = {XR_TYPE_FRAME_BEGIN_INFO};
    xrBeginFrame(session_, &begin_info);

    XrCompositionLayerProjection     projectionLayer    = {XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    XrCompositionLayerQuad           panelLayer         = {XR_TYPE_COMPOSITION_LAYER_QUAD};
    XrCompositionLayerProjectionView projectionViews[2] = {{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},
                                                           {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
    XrCompositionLayerDepthInfoKHR   depth_infos[2]{{XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR},
                                                    {XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR}};

    // XrCompositionLayerSpaceWarpInfoFB structures — allocated on the
    // stack and chained onto projectionViews[eye].next when spacewarp is active.
    XrCompositionLayerSpaceWarpInfoFB spacewarp_infos[2]{{XR_TYPE_COMPOSITION_LAYER_SPACE_WARP_INFO_FB},
                                                         {XR_TYPE_COMPOSITION_LAYER_SPACE_WARP_INFO_FB}};

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
            oxr_relay_->publish_boba_input(frame_state.predictedDisplayTime, frame_state.predictedDisplayPeriod,
                                           frame_state.shouldRender, view_state.viewStateFlags, views_, view_configs_);
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

            if (render_as_panel) {
                layers[0] = reinterpret_cast<XrCompositionLayerBaseHeader*>(&panelLayer);
            } else {
                projectionLayer.space      = local_space_;
                projectionLayer.viewCount  = 2;
                projectionLayer.views      = projectionViews;
                projectionLayer.layerFlags = XR_COMPOSITION_LAYER_CORRECT_CHROMATIC_ABERRATION_BIT |
                    XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT | XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
                layers[0] = reinterpret_cast<XrCompositionLayerBaseHeader*>(&projectionLayer);
            }
            layer_count = 1;
        }
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

    // Prefer an sRGB swapchain. The decoded video is converted back to linear
    // RGB in color.frag before this attachment applies its output transfer.
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
    spdlog::get("illixr")->info("Swapchain format selected: 0x{:X}", static_cast<uint32_t>(chosen_fmt));

    // Create one swapchain per eye
    for (int eye = 0; eye < 2; eye++) {
        swapchain_info& sc = swapchains_[eye];

        sc.format = chosen_fmt;
        sc.width  = I_HEADSET_WIDTH;  // Your input resolution
        sc.height = I_HEADSET_HEIGHT; // Your input resolution

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
                sc.width           = I_HEADSET_WIDTH;
                sc.height          = I_HEADSET_HEIGHT;

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

extern "C" plugin* this_plugin_factory(phonebook* pb) {
    auto* obj = new oxr_interface("openxr_interface", pb);
    // The runtime owns the plugin returned by this factory. Register a non-owning
    // service alias so the phonebook does not try to delete the same object again.
    pb->register_impl<vk::vulkan_context_provider>(std::shared_ptr<vk::vulkan_context_provider>(
        static_cast<vk::vulkan_context_provider*>(obj), [](vk::vulkan_context_provider*) { }));
    return obj;
}
#endif
