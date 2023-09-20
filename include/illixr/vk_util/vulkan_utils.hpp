#pragma once

#include <cassert>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <vector>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/hash.hpp>
#include <vulkan/vulkan_core.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#define VMA_STATIC_VULKAN_FUNCTIONS  0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include "third_party/vk_mem_alloc.h"
#pragma clang diagnostic pop

#define VK_ASSERT_SUCCESS(x)                                                                        \
    {                                                                                               \
        VkResult result = (x);                                                                      \
        if (result != VK_SUCCESS) {                                                                 \
            spdlog::get("illixr")->debug("[Vulkan] error: {}", vulkan_utils::error_string(result)); \
            throw std::runtime_error("Vulkan error: " + vulkan_utils::error_string(result));        \
        }                                                                                           \
    }

class vulkan_utils {
public:
    /**
     * @brief Returns the string representation of a VkResult.
     * @param err_code The VkResult to convert to a string.
     * @return The string representation of the VkResult.
     */
    static std::string error_string(VkResult err_code) {
        switch (err_code) {
        case VK_NOT_READY:
            return "VK_NOT_READY";
        case VK_TIMEOUT:
            return "VK_TIMEOUT";
        case VK_EVENT_SET:
            return "VK_EVENT_SET";
        case VK_EVENT_RESET:
            return "VK_EVENT_RESET";
        case VK_INCOMPLETE:
            return "VK_INCOMPLETE";
        case VK_ERROR_OUT_OF_HOST_MEMORY:
            return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY:
            return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED:
            return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST:
            return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED:
            return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT:
            return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT:
            return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT:
            return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER:
            return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_TOO_MANY_OBJECTS:
            return "VK_ERROR_TOO_MANY_OBJECTS";
        case VK_ERROR_FORMAT_NOT_SUPPORTED:
            return "VK_ERROR_FORMAT_NOT_SUPPORTED";
        case VK_ERROR_FRAGMENTED_POOL:
            return "VK_ERROR_FRAGMENTED_POOL";
        case VK_ERROR_UNKNOWN:
            return "VK_ERROR_UNKNOWN";
        case VK_ERROR_OUT_OF_POOL_MEMORY:
            return "VK_ERROR_OUT_OF_POOL_MEMORY";
        case VK_ERROR_INVALID_EXTERNAL_HANDLE:
            return "VK_ERROR_INVALID_EXTERNAL_HANDLE";
        case VK_ERROR_FRAGMENTATION:
            return "VK_ERROR_FRAGMENTATION";
        case VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS:
            return "VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS";
        case VK_ERROR_SURFACE_LOST_KHR:
            return "VK_ERROR_SURFACE_LOST_KHR";
        case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
            return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
        case VK_SUBOPTIMAL_KHR:
            return "VK_SUBOPTIMAL_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR:
            return "VK_ERROR_OUT_OF_DATE_KHR";
        case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR:
            return "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR";
        case VK_ERROR_VALIDATION_FAILED_EXT:
            return "VK_ERROR_VALIDATION_FAILED_EXT";
        case VK_ERROR_INVALID_SHADER_NV:
            return "VK_ERROR_INVALID_SHADER_NV";
        case VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT:
            return "VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT";
        case VK_ERROR_NOT_PERMITTED_EXT:
            return "VK_ERROR_NOT_PERMITTED_EXT";
        case VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT:
            return "VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT";
        default:
            return "UNKNOWN_ERROR";
        }
    }

    /**
     * @brief Creates a VkShaderModule from SPIR-V bytecode.
     *
     * @param device The Vulkan device to use.
     * @param code The SPIR-V bytecode.
     * @return The created VkShaderModule.
     */
    static VkShaderModule create_shader_module(VkDevice device, std::vector<char>&& code) {
        VkShaderModuleCreateInfo createInfo{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,   // sType
            nullptr,                                       // pNext
            0,                                             // flags
            code.size(),                                   // codeSize
            reinterpret_cast<const uint32_t*>(code.data()) // pCode
        };

        VkShaderModule shaderModule;
        VK_ASSERT_SUCCESS(vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule))

        return shaderModule;
    }

    /**
     * @brief Creates a VMA allocator.
     *
     * @param vk_instance The Vulkan instance to use.
     * @param vk_physical_device The Vulkan physical device to use.
     * @param vk_device The Vulkan device to use.
     * @return The created VMA allocator.
     */
    static VmaAllocator create_vma_allocator(VkInstance vk_instance, VkPhysicalDevice vk_physical_device, VkDevice vk_device) {
        VmaVulkanFunctions vulkanFunctions{};
        vulkanFunctions.vkGetInstanceProcAddr = &vkGetInstanceProcAddr;
        vulkanFunctions.vkGetDeviceProcAddr   = &vkGetDeviceProcAddr;

        VmaAllocatorCreateInfo allocatorCreateInfo{};
        allocatorCreateInfo.physicalDevice   = vk_physical_device;
        allocatorCreateInfo.device           = vk_device;
        allocatorCreateInfo.pVulkanFunctions = &vulkanFunctions;
        allocatorCreateInfo.instance         = vk_instance;
        allocatorCreateInfo.vulkanApiVersion = VK_API_VERSION_1_0;

        VmaAllocator allocator;
        VK_ASSERT_SUCCESS(vmaCreateAllocator(&allocatorCreateInfo, &allocator))
        return allocator;
    }

    /**
     * @brief Creates a one-time command buffer.
     *
     * @param vk_device The Vulkan device to use.
     * @param vk_command_pool The Vulkan command pool to use.
     * @return The created command buffer.
     */
    static VkCommandBuffer begin_one_time_command(VkDevice vk_device, VkCommandPool vk_command_pool) {
        VkCommandBufferAllocateInfo allocInfo{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, // sType
            nullptr,                                        // pNext
            vk_command_pool,                                // commandPool
            VK_COMMAND_BUFFER_LEVEL_PRIMARY,                // level
            1                                               // commandBufferCount
        };

        VkCommandBuffer commandBuffer;
        VK_ASSERT_SUCCESS(vkAllocateCommandBuffers(vk_device, &allocInfo, &commandBuffer))

        VkCommandBufferBeginInfo beginInfo{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, // sType
            nullptr,                                     // pNext
            VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, // flags
            nullptr                                      // pInheritanceInfo
        };
        VK_ASSERT_SUCCESS(vkBeginCommandBuffer(commandBuffer, &beginInfo))

        return commandBuffer;
    }

    /**
     * @brief Ends, submits and frees a one-time command buffer.
     *
     * @details This function waits for the queue to become idle.
     *
     * @param vk_device The Vulkan device to use.
     * @param vk_command_pool The Vulkan command pool to use.
     * @param vk_queue The Vulkan queue to use.
     * @param vk_command_buffer The Vulkan command buffer to use.
     */
    static void end_one_time_command(VkDevice vk_device, VkCommandPool vk_command_pool, VkQueue vk_queue,
                                     VkCommandBuffer vk_command_buffer) {
        VK_ASSERT_SUCCESS(vkEndCommandBuffer(vk_command_buffer))

        VkSubmitInfo submitInfo{
            VK_STRUCTURE_TYPE_SUBMIT_INFO, // sType
            nullptr,                       // pNext
            0,                             // waitSemaphoreCount
            nullptr,                       // pWaitSemaphores
            nullptr,                       // pWaitDstStageMask
            1,                             // commandBufferCount
            &vk_command_buffer,            // pCommandBuffers
            0,                             // signalSemaphoreCount
            nullptr                        // pSignalSemaphores
        };

        VK_ASSERT_SUCCESS(vkQueueSubmit(vk_queue, 1, &submitInfo, VK_NULL_HANDLE))
        VK_ASSERT_SUCCESS(vkQueueWaitIdle(vk_queue))

        vkFreeCommandBuffers(vk_device, vk_command_pool, 1, &vk_command_buffer);
    }

    /**
     * @brief Creates a VkCommandPool.
     *
     * @param device The Vulkan device to use.
     * @param queue_family_index The queue family index to use.
     * @return The created VkCommandPool.
     */
    static VkCommandPool create_command_pool(VkDevice device, uint32_t queue_family_index) {
        VkCommandPoolCreateInfo poolInfo = {
            VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,      // sType
            nullptr,                                         // pNext
            VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, // flags
            queue_family_index                               // queueFamilyIndex
        };

        VkCommandPool command_pool;
        VK_ASSERT_SUCCESS(vkCreateCommandPool(device, &poolInfo, nullptr, &command_pool))
        return command_pool;
    }

    /**
     * @brief Creates a VkCommandBuffer.
     *
     * @param device The Vulkan device to use.
     * @param command_pool The Vulkan command pool to use.
     */
    static VkCommandBuffer create_command_buffer(VkDevice device, VkCommandPool command_pool) {
        VkCommandBufferAllocateInfo allocInfo = {
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, // sType
            nullptr,                                        // pNext
            command_pool,                                   // commandPool
            VK_COMMAND_BUFFER_LEVEL_PRIMARY,                // level
            1                                               // commandBufferCount
        };

        VkCommandBuffer command_buffer;
        VK_ASSERT_SUCCESS(vkAllocateCommandBuffers(device, &allocInfo, &command_buffer))
        return command_buffer;
    }

    /**
     * @brief Reads a file into a vector of chars.
     *
     * @param path The path to the file.
     * @return The vector of chars.
     */
    static std::vector<char> read_file(const std::string& path) {
        std::ifstream file(path, std::ios::ate | std::ios::binary);

        if (!file.is_open()) {
            throw std::runtime_error("failed to open file!");
        }

        size_t            fileSize = (size_t) file.tellg();
        std::vector<char> buffer(fileSize);

        file.seekg(0);
        file.read(buffer.data(), static_cast<long>(fileSize));

        file.close();
        return buffer;
    }

    /**
     * @brief Copies a buffer to an image of the same size.
     *
     * @param vk_device The Vulkan device to use.
     * @param vk_queue The Vulkan queue to use.
     * @param vk_command_pool The Vulkan command pool to use.
     * @param buffer The buffer to copy from.
     * @param image The image to copy to.
     * @param width The width of the image.
     * @param height The height of the image.
     */
    static void copy_buffer_to_image(VkDevice vk_device, VkQueue vk_queue, VkCommandPool vk_command_pool, VkBuffer buffer,
                                     VkImage image, uint32_t width, uint32_t height) {
        VkCommandBuffer command_buffer = begin_one_time_command(vk_device, vk_command_pool);

        VkBufferImageCopy region{
            0, // bufferOffset
            0, // bufferRowLength
            0, // bufferImageHeight
            {
                // imageSubresource
                VK_IMAGE_ASPECT_COLOR_BIT, // aspectMask
                0,                         // mipLevel
                0,                         // baseArrayLayer
                1,                         // layerCount
            },
            {0, 0, 0},         // imageOffset
            {width, height, 1} // imageExtent
        };
        vkCmdCopyBufferToImage(command_buffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        end_one_time_command(vk_device, vk_command_pool, vk_queue, command_buffer);
    }
};
