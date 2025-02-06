#pragma once

#include "illixr/data_format.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"
#include "illixr/vk_util/display_sink.hpp"

namespace ILLIXR {
class display_vk : public display_sink {
public:
    explicit display_vk(const phonebook* const pb);

    /**
     * @brief This function sets up the GLFW and Vulkan environments. See display_sink::setup().
     */
    void setup();

    /**
     * @brief This function recreates the Vulkan swapchain. See display_sink::recreate_swapchain().
     */
    void recreate_swapchain() override;

    /**
     * @brief This function polls GLFW events. See display_sink::poll_window_events().
     */
    void poll_window_events() override;

private:
    /**
     * @brief Sets up the GLFW environment.
     *
     * This function initializes the GLFW library, sets the window hints for the client API and resizability,
     * and creates a GLFW window with the specified width and height.
     *
     * @throws runtime_error If GLFW initialization fails.
     */
    void setup_glfw();

    /**
     * @brief Sets up the Vulkan environment.
     *
     * This function initializes the Vulkan instance, selects the physical device, creates the Vulkan device,
     * gets the graphics and present queues, creates the swapchain, and sets up the VMA allocator.
     *
     * @throws runtime_error If any of the Vulkan setup steps fail.
     */
    void                               setup_vk();
    const std::shared_ptr<switchboard> switchboard_;
    vkb::Instance                      vkb_instance_;
    vkb::PhysicalDevice                physical_device_;
    vkb::Device                        vkb_device_;
    vkb::Swapchain                     vkb_swapchain_;

    std::atomic<bool> should_poll_{true};

    friend class display_vk_plugin;
};

class display_vk_plugin : public plugin {
public:
    [[maybe_unused]] display_vk_plugin(const std::string& name, phonebook* pb);
    void start() override;
    void stop() override;

private:
    void                        main_loop();
    std::thread                 main_thread_;
    std::atomic<bool>           ready_{false};
    std::shared_ptr<display_vk> display_vk_;
    std::atomic<bool>           running_{true};
    phonebook*                  phonebook_;
};
} // namespace ILLIXR