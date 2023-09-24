//
// Created by steven on 9/17/23.
//

#ifndef ILLIXR_X11_DIRECT_H
#define ILLIXR_X11_DIRECT_H

#include "display_backend.h"

#include <utility>

class x11_direct : public display_backend {
private:
    std::shared_ptr<RelativeClock>                              rc;
    switchboard::writer<switchboard::event_wrapper<time_point>> vsync_topic;
    VkFence                                                     display_event_fence = VK_NULL_HANDLE;
    VkPhysicalDevice                                            vk_physical_device;

public:
    x11_direct(std::shared_ptr<RelativeClock> _rc, switchboard::writer<switchboard::event_wrapper<time_point>> _vsync_topic)
        : rc{std::move(_rc)}
        , vsync_topic{_vsync_topic} {};
    void                       setup_display(VkInstance vk_instance, VkPhysicalDevice vk_physical_device) override;
    VkSurfaceKHR               create_surface() override;
    void                       cleanup() override;
    std::set<const char*>      get_required_instance_extensions() override;
    std::set<const char*>      get_required_device_extensions() override;
    VkDisplayModePropertiesKHR select_display_mode(std::vector<VkDisplayModePropertiesKHR> modes);
    void                       tick();
    bool                       register_display_timings_event(VkDevice vk_device);

    VkDisplayKHR               display;
    std::atomic<bool>          display_timings_event_registered = false;
    VkDevice                   vk_device;
    VkDisplayModePropertiesKHR selected_mode;
};

#endif // ILLIXR_X11_DIRECT_H
