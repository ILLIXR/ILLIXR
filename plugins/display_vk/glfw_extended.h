//
// Created by steven on 9/17/23.
//

#ifndef ILLIXR_GLFW_EXTENDED_H
#define ILLIXR_GLFW_EXTENDED_H

#include "display_backend.h"

class glfw_extended : public display_backend {
    void* window = nullptr;

public:
    /**
     * @brief Sets up the GLFW environment.
     *
     * This function initializes the GLFW library, sets the window hints for the client API and resizability,
     * and creates a GLFW window with the specified width and height.
     *
     * @throws runtime_error If GLFW initialization fails.
     */
    void setup_display(VkInstance vk_instance, VkPhysicalDevice vk_physical_device) override;

    void poll_window_events();

    std::pair<uint32_t, uint32_t> get_framebuffer_size();
    void                          cleanup() override;
    std::set<const char*>         get_required_instance_extensions() override;

private:
    VkSurfaceKHR create_surface() override;
};

#endif // ILLIXR_GLFW_EXTENDED_H
