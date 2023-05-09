#ifndef TIMEWARP_VK_VULKAN_UTILS_H
#define TIMEWARP_VK_VULKAN_UTILS_H

#include <cstdint>
#include <stdexcept>
#include <vector>
#include <fstream>
#include <iostream>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <vulkan/vulkan_core.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#pragma clang diagnostic push 
#pragma clang diagnostic ignored "-Weverything"
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include "third_party/vk_mem_alloc.h"
#pragma clang diagnostic pop

#define VK_ASSERT_SUCCESS(x) \
{ \
    VkResult result = (x); \
    if (result != VK_SUCCESS) { \
        std::cerr << "Vulkan error: " << vulkan_utils::error_string(result) << std::endl; \
        throw std::runtime_error("Vulkan error: " + vulkan_utils::error_string(result)); \
    } \
}

class vulkan_utils {
public:
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

    static VkShaderModule create_shader_module(VkDevice device, std::vector<char>&& code) {
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = code.size();
        createInfo.pCode    = reinterpret_cast<const uint32_t*>(code.data());

        VkShaderModule shaderModule;
        VK_ASSERT_SUCCESS(vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule));

        return shaderModule;
    }
    
    static VmaAllocator create_vma_allocator(VkInstance vk_instance, VkPhysicalDevice vk_physical_device, VkDevice vk_device) {
        VmaVulkanFunctions vulkanFunctions = {};
        vulkanFunctions.vkGetInstanceProcAddr = &vkGetInstanceProcAddr;
        vulkanFunctions.vkGetDeviceProcAddr = &vkGetDeviceProcAddr;
        
        VmaAllocatorCreateInfo allocatorCreateInfo = {};
        allocatorCreateInfo.vulkanApiVersion = VK_API_VERSION_1_2;
        allocatorCreateInfo.physicalDevice = vk_physical_device;
        allocatorCreateInfo.device = vk_device;
        allocatorCreateInfo.instance = vk_instance;
        allocatorCreateInfo.pVulkanFunctions = &vulkanFunctions;

        VmaAllocator allocator;
        VK_ASSERT_SUCCESS(vmaCreateAllocator(&allocatorCreateInfo, &allocator));
        return allocator;
    }

    static VkCommandBuffer begin_one_time_command(VkDevice vk_device, VkCommandPool vk_command_pool) {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool        = vk_command_pool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer;
        VK_ASSERT_SUCCESS(vkAllocateCommandBuffers(vk_device, &allocInfo, &commandBuffer));

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags            = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        VK_ASSERT_SUCCESS(vkBeginCommandBuffer(commandBuffer, &beginInfo));

        return commandBuffer;
    }

    static void end_one_time_command(VkDevice vk_device, VkCommandPool vk_command_pool, VkQueue vk_queue, VkCommandBuffer vk_command_buffer) {
        VK_ASSERT_SUCCESS(vkEndCommandBuffer(vk_command_buffer));

        VkSubmitInfo submitInfo{};
        submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers    = &vk_command_buffer;

        VK_ASSERT_SUCCESS(vkQueueSubmit(vk_queue, 1, &submitInfo, VK_NULL_HANDLE));
        VK_ASSERT_SUCCESS(vkQueueWaitIdle(vk_queue));

        vkFreeCommandBuffers(vk_device, vk_command_pool, 1, &vk_command_buffer);
    }

    static VkCommandPool create_command_pool(VkDevice device, uint32_t queue_family_index) {
        VkCommandPoolCreateInfo poolInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        poolInfo.queueFamilyIndex = queue_family_index;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

        VkCommandPool command_pool;
        VK_ASSERT_SUCCESS(vkCreateCommandPool(device, &poolInfo, nullptr, &command_pool));
        return command_pool;
    }

    static VkCommandBuffer create_command_buffer(VkDevice device, VkCommandPool command_pool) {
        VkCommandBufferAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        allocInfo.commandPool = command_pool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer command_buffer;
        VK_ASSERT_SUCCESS(vkAllocateCommandBuffers(device, &allocInfo, &command_buffer));
        return command_buffer;
    }

    static std::vector<char> read_file(std::string path) {
        std::ifstream file(path, std::ios::ate | std::ios::binary);

        if (!file.is_open()) {
            throw std::runtime_error("failed to open file!");
        }

        size_t fileSize = (size_t) file.tellg();
        std::vector<char> buffer(fileSize);

        file.seekg(0);
        file.read(buffer.data(), fileSize);

        file.close();
        return buffer;
    }

    static void copy_buffer_to_image(VkDevice vk_device, VkQueue vk_queue, VkCommandPool vk_command_pool, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) {
        VkCommandBuffer command_buffer = begin_one_time_command(vk_device, vk_command_pool);

        VkBufferImageCopy region{};
        region.bufferOffset      = 0;
        region.bufferRowLength   = 0;
        region.bufferImageHeight = 0;

        region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel       = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount     = 1;

        region.imageOffset = {0, 0, 0};
        region.imageExtent = {width, height, 1};

        vkCmdCopyBufferToImage(command_buffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        end_one_time_command(vk_device, vk_command_pool, vk_queue, command_buffer);
    }
};

#endif // TIMEWARP_VK_VULKAN_UTILS_H
