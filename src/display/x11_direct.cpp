//
// Created by steven on 9/17/23.
//
#include "x11_direct.h"

#include "illixr/vk/vulkan_utils.hpp"
#include "X11/extensions/Xrandr.h"
#include "X11/Xlib.h"

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_xlib_xrandr.h>

void x11_direct::setup_display(VkInstance vk_instance, VkPhysicalDevice vk_physical_device) {
    this->vk_instance        = vk_instance;
    this->vk_physical_device = vk_physical_device;

    Display* dpy = XOpenDisplay(nullptr);
    if (dpy == nullptr) {
        ILLIXR::abort("Failed to open X display");
        return;
    }

    uint32_t display_count;
    VK_ASSERT_SUCCESS(vkGetPhysicalDeviceDisplayPropertiesKHR(vk_physical_device, &display_count, nullptr));
    std::vector<VkDisplayPropertiesKHR> display_properties(display_count);
    VK_ASSERT_SUCCESS(vkGetPhysicalDeviceDisplayPropertiesKHR(vk_physical_device, &display_count, display_properties.data()));

    std::cout << "Found " << display_count << " displays:" << std::endl;
    int index = 0;
    for (const auto& display : display_properties) {
        std::cout << "\t[" << index++ << "] " << display.displayName << std::endl;
    }

    auto display_select_str = std::getenv("ILLIXR_DIRECT_MODE_DISPLAY");
    if (display_select_str == nullptr) {
        std::cout << "ILLIXR_DIRECT_MODE_DISPLAY not set, defaulting to the first display (" << display_properties[0].displayName << ")" << std::endl;
        display_select = 0;
    } else {
        display_select = std::stoi(display_select_str);
    }

    if (display_select >= display_count) {
        ILLIXR::abort("Invalid display selection");
        return;
    }

    display = display_properties[display_select].display;

    // Now we acquire the Xlib display
    auto acquire_xlib_display = (PFN_vkAcquireXlibDisplayEXT) vkGetInstanceProcAddr(vk_instance, "vkAcquireXlibDisplayEXT");
    if (acquire_xlib_display == nullptr) {
        ILLIXR::abort("Failed to load vkAcquireXlibDisplayEXT");
        return;
    }

    auto ret = acquire_xlib_display(vk_physical_device, dpy, display);
    if (ret != VK_SUCCESS) {
        ILLIXR::abort("Failed to acquire Xlib display");
        return;
    }
    std::cout << "Acquired Xlib display" << std::endl;
}

VkSurfaceKHR x11_direct::create_surface() {
    uint32_t plane_count;
    VK_ASSERT_SUCCESS(vkGetPhysicalDeviceDisplayPlanePropertiesKHR(vk_physical_device, &plane_count, nullptr));
    std::vector<VkDisplayPlanePropertiesKHR> plane_properties(plane_count);
    VK_ASSERT_SUCCESS(vkGetPhysicalDeviceDisplayPlanePropertiesKHR(vk_physical_device, &plane_count, plane_properties.data()));

    uint32_t plane_index = 0;
    for (const auto& plane : plane_properties) {
        if (plane.currentDisplay == nullptr) {
            break;
        }
        plane_index++;
    }

    if (plane_index == plane_count) {
        ILLIXR::abort("Failed to find display plane");
        return nullptr;
    }

    uint32_t mode_count;
    VK_ASSERT_SUCCESS(vkGetDisplayModePropertiesKHR(vk_physical_device, display, &mode_count, nullptr));
    std::vector<VkDisplayModePropertiesKHR> mode_properties(mode_count);
    VK_ASSERT_SUCCESS(vkGetDisplayModePropertiesKHR(vk_physical_device, display, &mode_count, mode_properties.data()));

    selected_mode = select_display_mode(mode_properties);

    VkDisplayPlaneCapabilitiesKHR plane_capabilities;
    VK_ASSERT_SUCCESS(
        vkGetDisplayPlaneCapabilitiesKHR(vk_physical_device, selected_mode.displayMode, plane_index, &plane_capabilities));

    VkDisplaySurfaceCreateInfoKHR surface_create_info = {
        .sType           = VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR,
        .pNext           = nullptr,
        .flags           = 0,
        .displayMode     = selected_mode.displayMode,
        .planeIndex      = plane_index,
        .planeStackIndex = plane_properties[plane_index].currentStackIndex,
        .transform       = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .globalAlpha     = 1.0f,
        .alphaMode       = VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR,
        .imageExtent     = selected_mode.parameters.visibleRegion,
    };

    VkSurfaceKHR surface;
    VK_ASSERT_SUCCESS(vkCreateDisplayPlaneSurfaceKHR(vk_instance, &surface_create_info, nullptr, &surface));

    return surface;
}

void x11_direct::cleanup() { }

std::set<const char*> x11_direct::get_required_instance_extensions() {
    std::set<const char*> extensions;
    extensions.insert(VK_KHR_SURFACE_EXTENSION_NAME);
    extensions.insert(VK_KHR_DISPLAY_EXTENSION_NAME);
    extensions.insert(VK_EXT_DIRECT_MODE_DISPLAY_EXTENSION_NAME);
    extensions.insert(VK_EXT_ACQUIRE_XLIB_DISPLAY_EXTENSION_NAME);
    extensions.insert(VK_EXT_DISPLAY_SURFACE_COUNTER_EXTENSION_NAME);
    return extensions;
}

VkDisplayModePropertiesKHR x11_direct::select_display_mode(std::vector<VkDisplayModePropertiesKHR> modes) {
    // select mode with highest refresh rate
    VkDisplayModePropertiesKHR selected_mode = modes[0];
    for (const auto& mode : modes) {
        if (mode.parameters.refreshRate > selected_mode.parameters.refreshRate) {
            selected_mode = mode;
        }
    }
    std::cout << "Selected display mode: " << selected_mode.parameters.visibleRegion.width << "x"
              << selected_mode.parameters.visibleRegion.height << "@" << selected_mode.parameters.refreshRate / 1000 << "Hz"
              << std::endl;
    return selected_mode;
}

void x11_direct::tick() {
    if (!display_timings_event_registered) {
        // yield
        std::this_thread::yield();
        return;
    }
    vkWaitForFences(vk_device, 1, &display_event_fence, VK_TRUE, UINT64_MAX);
    auto now = rc->now();
    vkDestroyFence(vk_device, display_event_fence, nullptr);
    register_display_timings_event(vk_device);

    now += std::chrono::nanoseconds(1000000000 / selected_mode.parameters.refreshRate);
    vsync_topic.put(vsync_topic.allocate(now));
}

bool x11_direct::register_display_timings_event(VkDevice vk_device) {
    this->vk_device = vk_device;
    auto register_display_event =
        (PFN_vkRegisterDisplayEventEXT) vkGetInstanceProcAddr(vk_instance, "vkRegisterDisplayEventEXT");
    if (register_display_event == nullptr) {
        ILLIXR::abort("Failed to load vkRegisterDisplayEventEXT");
        return false;
    }

    auto display_event_info = (VkDisplayEventInfoEXT){
        .sType        = VK_STRUCTURE_TYPE_DISPLAY_EVENT_INFO_EXT,
        .pNext        = nullptr,
        .displayEvent = VK_DISPLAY_EVENT_TYPE_FIRST_PIXEL_OUT_EXT,
    };

    auto ret = (register_display_event(vk_device, display, &display_event_info, nullptr, &display_event_fence));
    if (ret != VK_SUCCESS) {
        display_event_fence = VK_NULL_HANDLE;
    } else {
        display_timings_event_registered = true;
    }
    return ret == VK_SUCCESS;
}

std::set<const char*> x11_direct::get_required_device_extensions() {
    auto extensions = std::set<const char*>();
    extensions.insert(VK_EXT_DISPLAY_CONTROL_EXTENSION_NAME);
    return extensions;
}

display_backend::display_backend_type x11_direct::get_type() {
    return X11_DIRECT;
}
