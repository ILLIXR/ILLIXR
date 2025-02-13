#include "headless.hpp"

using namespace ILLIXR::display;

void headless::setup_display(VkInstance vk_instance, VkPhysicalDevice vk_physical_device) { (void)vk_instance; (void)vk_physical_device; }

VkSurfaceKHR headless::create_surface() {
    return VK_NULL_HANDLE;
}

void headless::cleanup() { }

std::set<const char*> headless::get_required_instance_extensions() {
    return {};
}

std::set<const char*> headless::get_required_device_extensions() {
    return {};
}

display_backend::display_backend_type headless::get_type() {
    return HEADLESS;
}
