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
 * - Hardware-accelerated HEVC decoding using FFmpeg/CUDA (Linux) or MediaCodec (Android)
 * - Support for stereo (left/right eye) rendering
 * - Optional depth frame reception and decoding (Linux)
 * - Image comparison mode for testing/debugging
 * - Performance metrics tracking
 *
 * Dependencies:
 * Linux:
 * - FFmpeg with CUDA support
 * - NVIDIA Video Codec SDK
 * - Vulkan SDK
 * - CUDA Toolkit
 *
 * Android:
 * - Android NDK with MediaCodec support
 * - EGL and OpenGL ES 3.0
 */

// ILLIXR core headers
#include "illixr/plugin.hpp"

#ifndef __ANDROID__

// ILLIXR Vulkan headers
#    include "illixr/vk/render_pass.hpp"
#    include "illixr/vk/vk_extension_request.hpp"

// FFmpeg headers (C interface)
extern "C" {
#    include "libavfilter_illixr/buffersink.h"
#    include "libavfilter_illixr/buffersrc.h"
#    include "libswscale_illixr/swscale.h"
}

#endif // !__ANDROID__

#include "offload_rendering_client.hpp"

#include <chrono>
#include <map>

using namespace ILLIXR;
#ifndef __ANDROID__
using namespace ILLIXR::vulkan::ffmpeg_utils;
#endif

/**
 * @class offload_rendering_client_loader
 * @brief Plugin loader for the offload rendering client (Linux)
 *
 * Handles plugin registration and Vulkan extension requirements.
 */
class offload_rendering_client_loader
    : public plugin
#ifndef __ANDROID__
    , public vulkan::vk_extension_request
#endif
{
public:
    /**
     * @brief Constructor registers the client plugin
     * @param name Plugin name
     * @param pb Phonebook for component lookup
     */
    [[maybe_unused]] offload_rendering_client_loader(const std::string& name, phonebook* pb)
        : plugin(name, pb)
        , offload_rendering_client_plugin{std::make_shared<offload_rendering_client>(name, pb)} {
#ifndef __ANDROID__
        pb->register_impl<vulkan::app>(offload_rendering_client_plugin);
#endif
    }

#ifndef __ANDROID__
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
#endif // !__ANDROID__
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
