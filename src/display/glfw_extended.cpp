//
// Created by steven on 9/17/23.
//

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "glfw_extended.h"

#include "illixr/error_util.hpp"

void glfw_extended::setup_display(VkInstance vk_instance, VkPhysicalDevice vk_physical_device) {
    this->vk_instance = vk_instance;
}

VkSurfaceKHR glfw_extended::create_surface() {
    VkSurfaceKHR surface;
    auto ret = glfwCreateWindowSurface(vk_instance, (GLFWwindow*) window, nullptr, &surface);
    if (ret != VK_SUCCESS) {
        // get the error code
        auto err = glfwGetError(nullptr);
        // print the error
        std::cerr << "glfw error: " << err << std::endl;

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
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions;
    glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    std::set<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
    return extensions;
}

glfw_extended::glfw_extended() {
    if (!glfwInit()) {
        ILLIXR::abort("Failed to initalize glfw");
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);

    window = glfwCreateWindow(display_params::width_pixels, display_params::height_pixels,
                              "ILLIXR Eyebuffer Window (Vulkan)", nullptr, nullptr);
}

std::set<const char*> glfw_extended::get_required_device_extensions() {
    return std::set<const char*>();
}

display_backend::display_backend_type glfw_extended::get_type() {
    return GLFW;
}
