//
// Created by steven on 9/17/23.
//

#include "glfw_extended.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "illixr/error_util.hpp"

void glfw_extended::setup_display(VkInstance vk_instance, VkPhysicalDevice vk_physical_device) {
    this->vk_instance = vk_instance;
    if (!glfwInit()) {
        ILLIXR::abort("Failed to initalize glfw");
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);

    window = glfwCreateWindow(display_params::width_pixels, display_params::height_pixels,
                              "ILLIXR Eyebuffer Window (Vulkan)", nullptr, nullptr);
}

VkSurfaceKHR glfw_extended::create_surface() {
    VkSurfaceKHR surface;
    if (glfwCreateWindowSurface(vk_instance, (GLFWwindow*) window, nullptr, &surface) != VK_SUCCESS) {
        throw std::runtime_error("failed to create window surface!");
    }
    return surface;
}

void glfw_extended::poll_window_events() {
    glfwPollEvents();
}

std::pair<uint32_t, uint32_t> glfw_extended::get_framebuffer_size() {
    int width, height;
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize((GLFWwindow*) window, &width, &height);
        glfwWaitEvents();
    }
    return {width, height};
}

void glfw_extended::cleanup() {
    glfwDestroyWindow((GLFWwindow*) window);
    glfwTerminate();
}

std::set<const char*> glfw_extended::get_required_instance_extensions() {
    return std::set<const char*>();
}
