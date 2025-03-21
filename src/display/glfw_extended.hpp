#pragma once

#include "display_backend.hpp"

namespace ILLIXR::display {

class glfw_extended : public display_backend {
public:
    glfw_extended();
    /**
     * @brief Sets up the GLFW environment.
     *
     * This function initializes the GLFW library, sets the window hints for the client API and resizability,
     * and creates a GLFW window with the specified width and height.
     *
     * @throws runtime_error If GLFW initialization fails.
     */
    void setup_display(const std::shared_ptr<switchboard> sb, VkInstance vk_instance,
                       VkPhysicalDevice vk_physical_device) override;

    static void poll_window_events();

    std::pair<uint32_t, uint32_t> get_framebuffer_size();
    void                          cleanup() override;
    std::set<const char*>         get_required_instance_extensions() override;
    std::set<const char*>         get_required_device_extensions() override;
    display_backend_type          get_type() override;

private:
    VkSurfaceKHR create_surface() override;

    void* window_ = nullptr;
};

} // namespace ILLIXR::display
