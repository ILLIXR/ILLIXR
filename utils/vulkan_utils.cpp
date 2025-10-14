#define VMA_IMPLEMENTATION
#include "vulkan_utils.hpp"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <utility>
#include <vector>
#include <vulkan/vulkan.h>

#ifdef __linux__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#define VMA_STATIC_VULKAN_FUNCTIONS  0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include "illixr/switchboard.hpp"
#include <vma/vk_mem_alloc.h>
#ifdef __linux__
#pragma clang diagnostic pop
#endif

using namespace ILLIXR::vulkan;

/**
 * @brief Returns the string representation of a VkResult.
 * @param err_code The VkResult to convert to a string.
 * @return The string representation of the VkResult.
 */
std::string ILLIXR::vulkan::error_string(VkResult err_code) {
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
VkShaderModule ILLIXR::vulkan::create_shader_module(VkDevice device, std::vector<char>&& code) {
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

VkSemaphore ILLIXR::vulkan::create_timeline_semaphore(VkDevice device, int initial_value,
                                                      VkExportSemaphoreCreateInfo* export_semaphore_create_info) {
    VkSemaphoreTypeCreateInfo timeline_create_info{};
    timeline_create_info.sType         = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    timeline_create_info.pNext         = export_semaphore_create_info;
    timeline_create_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    timeline_create_info.initialValue  = initial_value;

    VkSemaphoreCreateInfo semaphore_create_info{};
    semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semaphore_create_info.pNext = &timeline_create_info;
    semaphore_create_info.flags = 0;

    VkSemaphore semaphore;
    auto        ret = vkCreateSemaphore(device, &semaphore_create_info, nullptr, &semaphore);
    VK_ASSERT_SUCCESS(ret);

    return semaphore;
}

[[maybe_unused]] void ILLIXR::vulkan::wait_timeline_semaphore(VkDevice device, VkSemaphore semaphore, uint64_t value) {
    VkSemaphoreWaitInfo wait_info{};
    wait_info.sType          = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    wait_info.pNext          = nullptr;
    wait_info.flags          = 0;
    wait_info.semaphoreCount = 1;
    wait_info.pSemaphores    = &semaphore;
    wait_info.pValues        = &value;

    auto ret = vkWaitSemaphores(device, &wait_info, UINT64_MAX);
    VK_ASSERT_SUCCESS(ret);
}

void ILLIXR::vulkan::wait_timeline_semaphores(VkDevice device, const std::map<VkSemaphore, uint64_t>& semaphores) {
    VkSemaphoreWaitInfo wait_info{};
    wait_info.sType          = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    wait_info.pNext          = nullptr;
    wait_info.flags          = 0;
    wait_info.semaphoreCount = semaphores.size();
    std::vector<VkSemaphore> semaphore_vec;
    std::vector<uint64_t>    value_vec;
    for (auto& [semaphore, value] : semaphores) {
        semaphore_vec.push_back(semaphore);
        value_vec.push_back(value);
    }
    wait_info.pSemaphores = semaphore_vec.data();
    wait_info.pValues     = value_vec.data();

    auto ret = vkWaitSemaphores(device, &wait_info, UINT64_MAX);
    VK_ASSERT_SUCCESS(ret);
}

/**
 * @brief Creates a VMA allocator.
 *
 * @param vk_instance The Vulkan instance to use.
 * @param vk_physical_device The Vulkan physical device to use.
 * @param vk_device The Vulkan device to use.
 * @return The created VMA allocator.
 */
VmaAllocator ILLIXR::vulkan::create_vma_allocator(VkInstance vk_instance, VkPhysicalDevice vk_physical_device,
                                                  VkDevice vk_device) {
    VmaVulkanFunctions vulkanFunctions{};
    vulkanFunctions.vkGetInstanceProcAddr = &vkGetInstanceProcAddr;
    vulkanFunctions.vkGetDeviceProcAddr   = &vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo allocatorCreateInfo{};
    allocatorCreateInfo.physicalDevice   = vk_physical_device;
    allocatorCreateInfo.device           = vk_device;
    allocatorCreateInfo.pVulkanFunctions = &vulkanFunctions;
    allocatorCreateInfo.instance         = vk_instance;
    allocatorCreateInfo.vulkanApiVersion = VK_API_VERSION_1_2;

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
VkCommandBuffer ILLIXR::vulkan::begin_one_time_command(VkDevice vk_device, VkCommandPool vk_command_pool) {
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
void ILLIXR::vulkan::end_one_time_command(VkDevice vk_device, VkCommandPool vk_command_pool, const queue& q,
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

    std::lock_guard<std::mutex> lock(*q.mutex);
    VK_ASSERT_SUCCESS(vkQueueSubmit(q.vk_queue, 1, &submitInfo, VK_NULL_HANDLE))
    VK_ASSERT_SUCCESS(vkQueueWaitIdle(q.vk_queue))

    vkFreeCommandBuffers(vk_device, vk_command_pool, 1, &vk_command_buffer);
}

/**
 * @brief Creates a VkCommandPool.
 *
 * @param device The Vulkan device to use.
 * @param queue_family_index The queue family index to use.
 * @return The created VkCommandPool.
 */
VkCommandPool ILLIXR::vulkan::create_command_pool(VkDevice device, uint32_t queue_family_index) {
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
VkCommandBuffer ILLIXR::vulkan::create_command_buffer(VkDevice device, VkCommandPool command_pool) {
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

VkResult ILLIXR::vulkan::locked_queue_submit(queue& q, uint32_t submitCount, const VkSubmitInfo* pSubmits, VkFence fence) {
    std::lock_guard<std::mutex> lock(*q.mutex);
    return vkQueueSubmit(q.vk_queue, submitCount, pSubmits, fence);
}

/**
 * @brief Reads a file into a vector of chars.
 *
 * @param path The path to the file.
 * @return The vector of chars.
 */
std::vector<char> ILLIXR::vulkan::read_file(const std::string& path) {
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
void ILLIXR::vulkan::copy_buffer_to_image(VkDevice vk_device, queue q, VkCommandPool vk_command_pool, VkBuffer buffer,
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

    end_one_time_command(vk_device, vk_command_pool, std::move(q), command_buffer);
}

swapchain_details ILLIXR::vulkan::query_swapchain_details(VkPhysicalDevice const& physical_device,
                                                          VkSurfaceKHR const&     vk_surface) {
    swapchain_details details;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, vk_surface, &details.capabilities);

    uint32_t format_count;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, vk_surface, &format_count, nullptr);

    if (format_count != 0) {
        details.formats.resize(format_count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, vk_surface, &format_count, details.formats.data());
    }

    uint32_t present_mode_count;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, vk_surface, &present_mode_count, nullptr);

    if (present_mode_count != 0) {
        details.present_modes.resize(present_mode_count);
        vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, vk_surface, &present_mode_count,
                                                  details.present_modes.data());
    }

    return details;
}

queue_families ILLIXR::vulkan::find_queue_families(VkPhysicalDevice const& physical_device, VkSurfaceKHR const& vk_surface,
                                                   bool no_present) {
    queue_families indices;

    uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, queueFamilies.data());

    int i = 0;
    for (const auto& queue_family : queueFamilies) {
        if (queue_family.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphics_family = i;
        }

#if defined(VK_ENABLE_BETA_EXTENSIONS)
#endif
        if (queue_family.queueFlags & VK_QUEUE_COMPUTE_BIT) {
            indices.compute_family = i;
        }

        // if (queue_family.queueFlags & VK_QUEUE_TRANSFER_BIT) {
        //     if (!(queue_family.queueFlags & VK_QUEUE_GRAPHICS_BIT) && !(queue_family.queueFlags & VK_QUEUE_COMPUTE_BIT)) {
        //         indices.dedicated_transfer = i;
        //     }
        // }

        if (!no_present) {
            VkBool32 present_support = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, i, vk_surface, &present_support);

            if (present_support) {
                indices.present_family = i;
            }
        }

        if (indices.has_compression()) {
            break;
        }

        i++;
    }

    return indices;
}

VkImageView ILLIXR::vulkan::create_image_view(VkDevice device, VkImage image, VkFormat format,
                                              VkImageAspectFlags aspect_flags) {
    VkImageViewCreateInfo vk_image_view_create_info{};
    vk_image_view_create_info.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vk_image_view_create_info.image                           = image;
    vk_image_view_create_info.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    vk_image_view_create_info.format                          = format;
    vk_image_view_create_info.subresourceRange.aspectMask     = aspect_flags;
    vk_image_view_create_info.subresourceRange.baseMipLevel   = 0;
    vk_image_view_create_info.subresourceRange.levelCount     = 1;
    vk_image_view_create_info.subresourceRange.baseArrayLayer = 0;
    vk_image_view_create_info.subresourceRange.layerCount     = 1;
    vk_image_view_create_info.pNext                           = nullptr;

    VkImageView vk_image_view;
    VK_ASSERT_SUCCESS(vkCreateImageView(device, &vk_image_view_create_info, nullptr, &vk_image_view))
    return vk_image_view;
}
