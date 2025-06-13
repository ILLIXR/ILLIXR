/**
 * @file plugin.cpp
 * @brief ILLIXR Offload Rendering Client Plugin for Jetson
 *
 * This plugin implements the client-side functionality for offloaded rendering on Jetson devices.
 * It handles video decoding, pose synchronization, and Vulkan-based display integration.
 */
#define VULKAN_REQUIRED

#include "decoding/video_decode.h"
#include "illixr/data_format/pose_prediction.hpp"
#include "illixr/data_format/serializable_data.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"
#include "illixr/vk/display_provider.hpp"
#include "illixr/vk/render_pass.hpp"
#include "illixr/vk/vk_extension_request.hpp"
#include "illixr/vk/vulkan_utils.hpp"
#include "offload_rendering_client_jetson.hpp"

#include <bitset>
#include <cstdlib>
#include <set>

/**
 * @class offload_rendering_client_jetson_loader
 * @brief Plugin loader class for the offload rendering client
 *
 * Handles plugin registration and Vulkan extension setup
 */
class offload_rendering_client_jetson_loader
    : public plugin
    , public vulkan::vk_extension_request {
public:
    offload_rendering_client_jetson_loader(const std::string& name, phonebook* pb)
        : plugin(name, pb)
        , offload_rendering_client_jetson_plugin_{std::make_shared<offload_rendering_client_jetson>(name, pb)} {
        pb->register_impl<vulkan::app>(offload_rendering_client_jetson_plugin_);
    }

    std::vector<const char*> get_required_instance_extensions() override {
        return {VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME, VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME,
                VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME};
    }

    std::vector<const char*> get_required_devices_extensions() override {
        return {VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME, VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
                VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME, VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
                VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME};
    }

    void start() override {
        offload_rendering_client_jetson_plugin_->start();
    }

    void stop() override {
        offload_rendering_client_jetson_plugin_->stop();
    }

private:
    std::shared_ptr<offload_rendering_client_jetson> offload_rendering_client_jetson_plugin_;
};

PLUGIN_MAIN(offload_rendering_client_jetson_loader)
