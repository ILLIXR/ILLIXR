#ifndef TIMEWARP_VK_VULKAN_UTILS_H
#define TIMEWARP_VK_VULKAN_UTILS_H

#include <stdexcept>
#include <vulkan/vulkan_core.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#pragma clang diagnostic push 
#pragma clang diagnostic ignored "-Weverything"
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include "third_party/vk_mem_alloc.h"
#pragma clang diagnostic pop

class vulkan_utils {
public:
    static VkShaderModule create_shader_module(VkDevice device, std::vector<char>&& code) {
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = code.size();
        createInfo.pCode    = reinterpret_cast<const uint32_t*>(code.data());

        VkShaderModule shaderModule;
        if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
            throw std::runtime_error("failed to create shader module!");
        }

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
        vmaCreateAllocator(&allocatorCreateInfo, &allocator);
        return allocator;
    }

    static VkCommandBuffer begin_one_time_command(VkDevice vk_device, VkCommandPool vk_command_pool) {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool        = vk_command_pool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer;
        vkAllocateCommandBuffers(vk_device, &allocInfo, &commandBuffer);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags            = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(commandBuffer, &beginInfo);

        return commandBuffer;
    }

    static VkCommandBuffer end_one_time_command(VkDevice vk_device, VkCommandPool vk_command_pool, VkQueue vk_queue, VkCommandBuffer vk_command_buffer) {
        vkEndCommandBuffer(vk_command_buffer);

        VkSubmitInfo submitInfo{};
        submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers    = &vk_command_buffer;

        vkQueueSubmit(vk_queue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(vk_queue);

        vkFreeCommandBuffers(vk_device, vk_command_pool, 1, &vk_command_buffer);
    }
};

#endif // TIMEWARP_VK_VULKAN_UTILS_H
