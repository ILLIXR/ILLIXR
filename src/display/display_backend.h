//
// Created by steven on 9/17/23.
//

#ifndef ILLIXR_DISPLAY_BACKEND_H
#define ILLIXR_DISPLAY_BACKEND_H

#include "illixr/vk/display_provider.hpp"

class display_backend {
protected:
    VkInstance vk_instance;

public:
    enum display_backend_type {
        GLFW,
        X11_DIRECT,
        HEADLESS
    };

    virtual void         setup_display(VkInstance vk_instance, VkPhysicalDevice vk_physical_device) = 0;
    virtual VkSurfaceKHR create_surface() = 0;
    virtual void         cleanup()        = 0;

    virtual std::set<const char*> get_required_instance_extensions() = 0;
    virtual std::set<const char*> get_required_device_extensions()   = 0;

    virtual display_backend_type get_type() = 0;
};

#endif // ILLIXR_DISPLAY_BACKEND_H
