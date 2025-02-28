#pragma once

#include "display_backend.hpp"
#include "illixr/switchboard.hpp"

#include <utility>

namespace ILLIXR::display {

class x11_direct : public display_backend {
public:
    x11_direct(std::shared_ptr<relative_clock> _rc, switchboard::writer<switchboard::event_wrapper<time_point>> _vsync_topic)
        : clock_{std::move(_rc)}
        , vsync_topic_{_vsync_topic} { };
    void                              setup_display(const std::shared_ptr<switchboard> sb, VkInstance vk_instance,
                                                    VkPhysicalDevice vk_physical_device_) override;
    VkSurfaceKHR                      create_surface() override;
    void                              cleanup() override;
    std::set<const char*>             get_required_instance_extensions() override;
    std::set<const char*>             get_required_device_extensions() override;
    static VkDisplayModePropertiesKHR select_display_mode(std::vector<VkDisplayModePropertiesKHR> modes);
    void                              tick();
    bool                              register_display_timings_event(VkDevice vk_device_);
    display_backend_type              get_type() override;

    VkDisplayKHR               display_;
    std::atomic<bool>          display_timings_event_registered_ = false;
    VkDevice                   vk_device_;
    VkDisplayModePropertiesKHR selected_mode_;

private:
    std::shared_ptr<relative_clock>                             clock_;
    switchboard::writer<switchboard::event_wrapper<time_point>> vsync_topic_;
    VkFence                                                     display_event_fence_ = VK_NULL_HANDLE;
    VkPhysicalDevice                                            vk_physical_device_;
};

} // namespace ILLIXR::display
