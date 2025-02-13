/**
 * @file plugin.cpp
 * @brief Offload Rendering Server Plugin Implementation
 *
 * This plugin implements the server-side component of ILLIXR's offload rendering system.
 * It captures rendered frames, encodes them using hardware-accelerated H.264/HEVC encoding,
 * and sends them to a remote client for display. The system supports both color and depth
 * frame transmission, with configurable encoding parameters.
 */

#include "plugin.hpp"

#include "illixr/pose_prediction.hpp"
#include "illixr/vk/render_pass.hpp"

using namespace ILLIXR;
using namespace ILLIXR::vulkan::ffmpeg_utils;

[[maybe_unused]] offload_rendering_server_loader::offload_rendering_server_loader(const std::string& name, phonebook* pb)
    : plugin(name, pb)
    , offload_rendering_server_plugin_{std::make_shared<offload_rendering_server>(name, pb)} {
    pb->register_impl<vulkan::timewarp>(offload_rendering_server_plugin_);
    pb->register_impl<pose_prediction>(offload_rendering_server_plugin_);
    log_->info("Registered vulkan::timewarp and pose_prediction implementations");
}

std::vector<const char*> offload_rendering_server_loader::get_required_instance_extensions() {
    return {VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME, VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME,
            VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME};
}

std::vector<const char*> offload_rendering_server_loader::get_required_devices_extensions() {
    return {VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME, VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
            VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME, VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
            VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME};
}

void offload_rendering_server_loader::start() {
    offload_rendering_server_plugin_->start();
}

void offload_rendering_server_loader::stop() {
    offload_rendering_server_plugin_->stop();
}

PLUGIN_MAIN(offload_rendering_server_loader)
