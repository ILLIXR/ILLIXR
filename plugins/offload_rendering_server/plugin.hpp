#pragma once

#include "illixr/plugin.hpp"
#include "illixr/vk/vk_extension_request.hpp"
#include "offload_rendering_server.hpp"

namespace ILLIXR {

/**
 * @class offload_rendering_server_loader
 * @brief Plugin loader for the offload rendering server
 *
 * Handles plugin registration and Vulkan extension requirements for the
 * offload rendering server component.
 */
class offload_rendering_server_loader
    : public plugin
    , public vulkan::vk_extension_request {
public:
    /**
     * @brief Constructor registers the server plugin with the system
     * @param name Plugin name
     * @param pb Phonebook for component lookup
     */
    [[maybe_unused]] offload_rendering_server_loader(const std::string& name, phonebook* pb);

    /**
     * @brief Get required Vulkan instance extensions
     * @return List of required instance extension names
     */
    std::vector<const char*> get_required_instance_extensions() override;

    /**
     * @brief Get required Vulkan device extensions
     * @return List of required device extension names
     */
    std::vector<const char*> get_required_devices_extensions() override;

    void start() override;

    void stop() override;

private:
    std::shared_ptr<offload_rendering_server> offload_rendering_server_plugin_;
    std::shared_ptr<spdlog::logger>           log_{spdlogger("debug")};
};

} // namespace ILLIXR
