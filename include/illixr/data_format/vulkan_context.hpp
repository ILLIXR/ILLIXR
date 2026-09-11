#pragma once

// This header is intentionally minimal so it can be included by any plugin
// without pulling in large Vulkan or ILLIXR framework headers.  Plugins that
// need the full Vulkan API must include <vulkan/vulkan.h> themselves before
// including this file (or rely on their own transitive includes).

#include <cstdint>
#include <vulkan/vulkan.h>

namespace ILLIXR::data_format {

/**
 * @brief Vulkan device context published to the phonebook by the OpenXR
 *        session owner (oxr_interface) for use by other plugins.
 *
 * On Android + OpenXR the Vulkan device is created by the OpenXR runtime
 * (via xrCreateVulkanDeviceKHR / XR_KHR_vulkan_enable2) and is not
 * accessible through any other ILLIXR service.  oxr_interface registers an
 * instance of this struct after create_session() completes; any plugin that
 * needs a VkDevice can call phonebook::lookup_impl<vulkan_device_context>()
 * without any direct dependency on oxr_interface.
 *
 * The handles are owned by oxr_interface for the lifetime of the XrSession;
 * consumers must not destroy them.
 */
struct vulkan_device_context {
    VkInstance       instance        = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice         device          = VK_NULL_HANDLE;
    VkQueue          queue           = VK_NULL_HANDLE;
    uint32_t         queue_family    = 0;
};

} // namespace ILLIXR::data_format
