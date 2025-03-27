/**
 * @file plugin.cpp
 * @brief Offload Rendering Client Plugin Implementation
 *
 * This plugin implements the client-side component of ILLIXR's offload rendering system.
 * It receives encoded frames from a remote server, decodes them using hardware-accelerated
 * HEVC decoding, and displays them in VR. The system supports both color and depth frame
 * reception and decoding.
 *
 * Key features:
 * - Hardware-accelerated HEVC decoding using FFmpeg/CUDA
 * - Support for stereo (left/right eye) rendering
 * - Optional depth frame reception and decoding
 * - Image comparison mode for testing/debugging
 * - Performance metrics tracking
 *
 * Dependencies:
 * - FFmpeg with CUDA support
 * - NVIDIA Video Codec SDK
 * - Vulkan SDK
 * - CUDA Toolkit
 */

// ILLIXR core headers
#include "illixr/plugin.hpp"

// ILLIXR Vulkan headers
#include "illixr/vk/render_pass.hpp"
#include "illixr/vk/vk_extension_request.hpp"

// FFmpeg headers (C interface)
extern "C" {
#include "libavfilter_illixr/buffersink.h"
#include "libavfilter_illixr/buffersrc.h"
#include "libswscale_illixr/swscale.h"
}

#include "offload_rendering_client.hpp"

#include <chrono>
#include <map>
/**
 * @brief Name of the FFmpeg decoder to use (HEVC/H.265)
 */

using namespace ILLIXR;
using namespace ILLIXR::vulkan::ffmpeg_utils;

/**
 * @class offload_rendering_client_loader
 * @brief Plugin loader for the offload rendering client
 *
 * Handles plugin registration and Vulkan extension requirements.
 */
class offload_rendering_client_loader
    : public plugin
    , public vulkan::vk_extension_request {
public:
    /**
     * @brief Constructor registers the client plugin
     * @param name Plugin name
     * @param pb Phonebook for component lookup
     */
    [[maybe_unused]] offload_rendering_client_loader(const std::string& name, phonebook* pb)
        : plugin(name, pb)
        , offload_rendering_client_plugin{std::make_shared<offload_rendering_client>(name, pb)} {
        pb->register_impl<vulkan::app>(offload_rendering_client_plugin);
    }

    /**
     * @brief Get required Vulkan instance extensions
     * @return Vector of required extension names
     */
    std::vector<const char*> get_required_instance_extensions() override {
        return {VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME, VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME,
                VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME};
    }

    /**
     * @brief Get required Vulkan device extensions
     * @return Vector of required extension names
     */
    std::vector<const char*> get_required_devices_extensions() override {
        return {VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME, VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
                VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME, VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
                VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME};
    }

    /**
     * @brief Start the client plugin
     */
    void start() override {
        offload_rendering_client_plugin->start();
    }

    /**
     * @brief Stop the client plugin
     */
    void stop() override {
        offload_rendering_client_plugin->stop();
    }

private:
    std::shared_ptr<offload_rendering_client> offload_rendering_client_plugin;
};

PLUGIN_MAIN(offload_rendering_client_loader)
