#pragma once

#include "display_backend.hpp"

namespace ILLIXR::display {

class headless : public display_backend {
public:
    void                  setup_display(const std::shared_ptr<switchboard> sb, VkInstance vk_instance,
                                        VkPhysicalDevice vk_physical_device) override;
    VkSurfaceKHR          create_surface() override;
    void                  cleanup() override;
    std::set<const char*> get_required_instance_extensions() override;
    std::set<const char*> get_required_device_extensions() override;
    display_backend_type  get_type() override;
};

} // namespace ILLIXR::display
