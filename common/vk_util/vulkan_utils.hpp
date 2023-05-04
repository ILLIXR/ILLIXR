#ifndef TIMEWARP_VK_VULKAN_UTILS_H
#define TIMEWARP_VK_VULKAN_UTILS_H

#include <stdexcept>
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
};

#endif // TIMEWARP_VK_VULKAN_UTILS_H
