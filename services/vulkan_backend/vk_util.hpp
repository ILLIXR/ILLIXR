#pragma once

#include <string>

#include <vulkan/vulkan.h>

/**
 * @brief Returns the string representation of a VkResult.
 * @param err_code The VkResult to convert to a string.
 * @return The string representation of the VkResult.
 */
std::string vk_error_string(VkResult err_code);

#define VK_ASSERT_SUCCESS(x)                                                                \
    {                                                                                       \
        VkResult result = (x);                                                              \
        if (result != VK_SUCCESS) {                                                         \
            spdlog::get("illixr")->debug("[Vulkan] error: {}", vk_error_string(result));    \
            throw std::runtime_error("Vulkan error: " + vk_error_string(result));           \
        }                                                                                   \
    }