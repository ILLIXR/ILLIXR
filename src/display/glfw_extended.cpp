#define GLFW_INCLUDE_VULKAN
#include "glfw_extended.hpp"

#include "illixr/error_util.hpp"

#include <GLFW/glfw3.h>

using namespace ILLIXR::display;

void glfw_extended::setup_display(const std::shared_ptr<switchboard> sb, VkInstance vk_instance,
                                  VkPhysicalDevice vk_physical_device) {
    (void) sb;
    (void) vk_physical_device;
    this->vk_instance_ = vk_instance;
}

VkSurfaceKHR glfw_extended::create_surface() {
    VkSurfaceKHR surface;
    auto         ret = glfwCreateWindowSurface(vk_instance_, (GLFWwindow*) window_, nullptr, &surface);
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
        glfwGetFramebufferSize((GLFWwindow*) window_, &width, &height);
        glfwWaitEvents();
    }
    return {width, height};
}

void glfw_extended::cleanup() {
    // The display backend is always responsible for terminating GLFW.
    glfwDestroyWindow((GLFWwindow*) window_);
    glfwTerminate();
}

std::set<const char*> glfw_extended::get_required_instance_extensions() {
    uint32_t     glfwExtensionCount = 0;
    const char** glfwExtensions;
    glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    std::set<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
    return extensions;
}

glfw_extended::glfw_extended() {
    if (!glfwInit()) {
        ILLIXR::abort("Failed to initialize glfw");
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);

    window_ = glfwCreateWindow(display_params::width_pixels, display_params::height_pixels, "ILLIXR Eyebuffer Window (Vulkan)",
                               nullptr, nullptr);

    // Get the primary monitor
    GLFWmonitor* primary_monitor = glfwGetPrimaryMonitor();
    if (primary_monitor) {
        // Get the video mode of the primary monitor
        const GLFWvidmode* mode = glfwGetVideoMode(primary_monitor);
        // Set the window to fullscreen on the primary monitor
        glfwSetWindowMonitor((GLFWwindow*) window_, primary_monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
    }
}

std::set<const char*> glfw_extended::get_required_device_extensions() {
    return {};
}

display_backend::display_backend_type glfw_extended::get_type() {
    return GLFW;
}
