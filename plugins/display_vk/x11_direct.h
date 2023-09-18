//
// Created by steven on 9/17/23.
//

#ifndef ILLIXR_X11_DIRECT_H
#define ILLIXR_X11_DIRECT_H

#include "display_backend.h"

class x11_direct : public display_backend{
public:
    void                  setup_display(VkInstance vk_instance, VkPhysicalDevice vk_physical_device) override;
    VkSurfaceKHR          create_surface() override;
    void                  cleanup() override;
    std::set<const char*> get_required_instance_extensions() override;
    VkDisplayKHR          display;
    VkPhysicalDevice      vk_physical_device;
};

#endif // ILLIXR_X11_DIRECT_H
