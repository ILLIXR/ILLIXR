//
// Created by steven on 9/17/23.
//

#ifndef ILLIXR_WAYLAND_DRM_H
#define ILLIXR_WAYLAND_DRM_H

#include "display_backend.h"
#include "illixr/error_util.hpp"

#include <atomic>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include <wayland-client.h>
#include <wayland-drm-lease-v1-client-protocol.h>
#include <xf86drm.h>

struct wayland_drm_lease_device_connector {
    struct wp_drm_lease_connector_v1* connector;
    std::string                       name;
    std::string                       description;
    uint32_t                          connector_id;
};

struct wayland_drm_lease_device {
    struct wp_drm_lease_device_v1*                                   device;
    int                                                              fd;
    std::vector<std::shared_ptr<wayland_drm_lease_device_connector>> connectors;
    std::atomic<bool>                                                done{false};

    wayland_drm_lease_device(struct wp_drm_lease_device_v1* device, int fd)
        : device(device)
        , fd(fd) { }
};

class wayland_drm : public display_backend {
    static void device_listener_drm_fd(void* data, struct wp_drm_lease_device_v1* wp_drm_lease_device_v1, int32_t fd) {
        auto this_                                            = reinterpret_cast<wayland_drm*>(data);
        this_->get_device_wrapper(wp_drm_lease_device_v1)->fd = fd;
    }

    static void connector_listener_name(void* data, struct wp_drm_lease_connector_v1* wp_drm_lease_connector_v1,
                                        const char* name) {
        auto this_      = reinterpret_cast<wayland_drm*>(data);
        auto connector  = this_->get_connector_wrapper(wp_drm_lease_connector_v1);
        connector->name = name;
    }

    static void connector_listener_description(void* data, struct wp_drm_lease_connector_v1* wp_drm_lease_connector_v1,
                                               const char* description) {
        auto this_             = reinterpret_cast<wayland_drm*>(data);
        auto connector         = this_->get_connector_wrapper(wp_drm_lease_connector_v1);
        connector->description = description;
    }

    static void connector_listener_done(void* data, struct wp_drm_lease_connector_v1* wp_drm_lease_connector_v1) {
        auto this_     = reinterpret_cast<wayland_drm*>(data);
        auto connector = this_->get_connector_wrapper(wp_drm_lease_connector_v1);
        std::cout << "Lease connector " << connector->name << "(" << connector->description << ") done" << std::endl;
    }

    static void connector_listener_withdrawn(void* data, struct wp_drm_lease_connector_v1* wp_drm_lease_connector_v1) {
        auto this_     = reinterpret_cast<wayland_drm*>(data);
        auto connector = this_->get_connector_wrapper(wp_drm_lease_connector_v1);
        std::cerr << "Lease connector " << connector->name << "(" << connector->description << ") withdrawn" << std::endl;
    }

    static void connector_listener_connector_id(void* data, struct wp_drm_lease_connector_v1* wp_drm_lease_connector_v1,
                                                uint32_t connector_id) {
        auto this_              = reinterpret_cast<wayland_drm*>(data);
        auto connector          = this_->get_connector_wrapper(wp_drm_lease_connector_v1);
        connector->connector_id = connector_id;
    }

    constexpr static const wp_drm_lease_connector_v1_listener connector_listener = {
        .name         = connector_listener_name,
        .description  = connector_listener_description,
        .connector_id = connector_listener_connector_id,
        .done         = connector_listener_done,
        .withdrawn    = connector_listener_withdrawn};

    static void device_listener_connector(void* data, struct wp_drm_lease_device_v1* wp_drm_lease_device_v1,
                                          struct wp_drm_lease_connector_v1* wp_drm_lease_connector_v1) {
        auto this_           = reinterpret_cast<wayland_drm*>(data);
        auto device          = this_->get_device_wrapper(wp_drm_lease_device_v1);
        auto connector       = std::make_shared<wayland_drm_lease_device_connector>();
        connector->connector = wp_drm_lease_connector_v1;
        device->connectors.push_back(connector);
        wp_drm_lease_connector_v1_add_listener(wp_drm_lease_connector_v1, &connector_listener, this_);
    }

    static void device_listener_done(void* data, struct wp_drm_lease_device_v1* wp_drm_lease_device_v1) {
        auto this_   = reinterpret_cast<wayland_drm*>(data);
        auto device  = this_->get_device_wrapper(wp_drm_lease_device_v1);
        device->done = true;
        std::cout << "Lease device done" << std::endl;
    }

    static void device_listener_released(void* data, struct wp_drm_lease_device_v1* wp_drm_lease_device_v1) {
        auto this_  = reinterpret_cast<wayland_drm*>(data);
        auto device = this_->get_device_wrapper(wp_drm_lease_device_v1);
        std::cerr << "Lease device released" << std::endl;
        // remove device from list
        this_->lease_devices.erase(std::remove(this_->lease_devices.begin(), this_->lease_devices.end(), device),
                                   this_->lease_devices.end());
    }
    constexpr static const struct wp_drm_lease_device_v1_listener lease_device_listener = {
        .drm_fd    = device_listener_drm_fd,
        .connector = device_listener_connector,
        .done      = device_listener_done,
        .released  = device_listener_released};
    
    static void global(wayland_drm* this_, struct wl_registry* wl_registry, uint32_t name, const char* interface,
                       uint32_t version) {
        if (strcmp(interface, wp_drm_lease_device_v1_interface.name) == 0) {
            auto device =
                std::make_shared<wayland_drm_lease_device>(reinterpret_cast<wp_drm_lease_device_v1*>(wl_registry_bind(
                                                               wl_registry, name, &wp_drm_lease_device_v1_interface, 1)),
                                                           -1);
            this_->lease_devices.push_back(device);
            wp_drm_lease_device_v1_add_listener(device->device, &lease_device_listener, this_);
        }
    }
    
    static void global_remove(wayland_drm* this_, struct wl_registry* wl_registry, uint32_t name) {
        // for a one-time query, this is not needed
    }

    std::shared_ptr<wayland_drm_lease_device> get_device_wrapper(struct wp_drm_lease_device_v1 *device);

    std::shared_ptr<wayland_drm_lease_device_connector> get_connector_wrapper(struct wp_drm_lease_connector_v1 *connector);

    std::vector<std::shared_ptr<wayland_drm_lease_device>> lease_devices;

public:
    void setup_display(VkInstance vk_instance, VkPhysicalDevice vk_physical_device) override;
};

#endif // ILLIXR_WAYLAND_DRM_H
