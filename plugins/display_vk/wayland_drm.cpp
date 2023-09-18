//
// Created by steven on 9/17/23.
//

#include "wayland_drm.h"

#include "illixr/data_format.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"
#include "illixr/vk_util/display_sink.hpp"

#include <wayland-client.h>
#include <wayland-drm-lease-v1-client-protocol.h>
#include <xf86drm.h>

void wayland_drm::setup_display(VkInstance vk_instance, VkPhysicalDevice vk_physical_device) {
    auto display = wl_display_connect(nullptr);
    if (!display) {
        ILLIXR::abort("Failed to connect to Wayland display");
    }

    auto registry = wl_display_get_registry(display);
    if (!registry) {
        ILLIXR::abort("Failed to get Wayland registry");
    }

    const struct wl_registry_listener registry_listener = {
        .global = reinterpret_cast<void (*)(void*, wl_registry*, uint32_t, const char*, uint32_t)>(wayland_drm::global),
        .global_remove = reinterpret_cast<void (*)(void*, wl_registry*, uint32_t)>(wayland_drm::global_remove)};

    wl_registry_add_listener(registry, &registry_listener, this);

    wl_display_roundtrip(display);

    for (auto device_wrapper : lease_devices) {
        while (!device_wrapper->done) {
            wl_display_dispatch(display);
        }
    }

    wl_registry_destroy(registry);

    // print out devices
    for (auto device_wrapper : lease_devices) {
        std::cout << "Lease device " << device_wrapper->fd << std::endl;
        for (auto connector_wrapper : device_wrapper->connectors) {
            std::cout << "Lease connector " << connector_wrapper->name << "(" << connector_wrapper->description << ")" << std::endl;
        }
    }

    std::cout << "Waiting for lease devices to be done" << std::endl;
}

std::shared_ptr<wayland_drm_lease_device_connector>
wayland_drm::get_connector_wrapper(struct wp_drm_lease_connector_v1* connector) {
    for (auto device_wrapper : lease_devices) {
        for (auto connector_wrapper : device_wrapper->connectors) {
            if (connector_wrapper->connector == connector) {
                return connector_wrapper;
            }
        }
    }
    return nullptr;
}

std::shared_ptr<wayland_drm_lease_device> wayland_drm::get_device_wrapper(struct wp_drm_lease_device_v1* device) {
    for (auto device_wrapper : lease_devices) {
        if (device_wrapper->device == device) {
            return device_wrapper;
        }
    }
    return nullptr;
}
