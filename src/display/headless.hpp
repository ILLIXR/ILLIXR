//
// Created by steven on 11/14/23.
//

#ifndef ILLIXR_HEADLESS_HPP
#define ILLIXR_HEADLESS_HPP

#include "display_backend.h"

class headless : public display_backend {
public:
    void                  setup_display(VkInstance vk_instance, VkPhysicalDevice vk_physical_device) override;
    VkSurfaceKHR          create_surface() override;
    void                  cleanup() override;
    std::set<const char*> get_required_instance_extensions() override;
    std::set<const char*> get_required_device_extensions() override;
    display_backend_type  get_type() override;
};

#endif // ILLIXR_HEADLESS_HPP
