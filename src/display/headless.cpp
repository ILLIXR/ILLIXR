//
// Created by steven on 11/14/23.
//

#include "headless.hpp"

void headless::setup_display(VkInstance vk_instance, VkPhysicalDevice vk_physical_device) { }

VkSurfaceKHR headless::create_surface() {
    return VK_NULL_HANDLE;
}

void headless::cleanup() { }

std::set<const char*> headless::get_required_instance_extensions() {
    return std::set<const char*>();
}

std::set<const char*> headless::get_required_device_extensions() {
    return std::set<const char*>();
}

display_backend::display_backend_type headless::get_type() {
    return HEADLESS;
}
