//
// Created by steven on 9/17/23.
//
#include <vulkan/vulkan.h>
#include "X11/Xlib.h"
#include "X11/extensions/Xrandr.h"
#include <vulkan/vulkan_xlib_xrandr.h>

#include "x11_direct.h"

void x11_direct::setup_display(VkInstance vk_instance, VkPhysicalDevice vk_physical_device) {
    this->vk_instance = vk_instance;
    this->vk_physical_device = vk_physical_device;

    Display *dpy = XOpenDisplay(nullptr);
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

    auto display_select_str = std::getenv("ILLIXR_DIRECT_MODE");
    assert(display_select_str != nullptr);
    int display_select = std::stoi(display_select_str);
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
}

VkSurfaceKHR x11_direct::create_surface() {
    uint32_t plane_count;
    VK_ASSERT_SUCCESS(vkGetPhysicalDeviceDisplayPlanePropertiesKHR(vk_physical_device, &plane_count, nullptr));
    std::vector<VkDisplayPlanePropertiesKHR> plane_properties(plane_count);
    VK_ASSERT_SUCCESS(vkGetPhysicalDeviceDisplayPlanePropertiesKHR(vk_physical_device, &plane_count, plane_properties.data()));


    return nullptr;
}

void x11_direct::cleanup() { }

std::set<const char*> x11_direct::get_required_instance_extensions() {
    std::set<const char*> extensions;
    extensions.insert(VK_EXT_DIRECT_MODE_DISPLAY_EXTENSION_NAME);
    extensions.insert(VK_EXT_ACQUIRE_XLIB_DISPLAY_EXTENSION_NAME);
    return extensions;
}