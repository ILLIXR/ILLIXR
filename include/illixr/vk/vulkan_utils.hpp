#pragma once

#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <utility>
#include <vector>
#include <vulkan/vulkan.h>

#define VK_ASSERT_SUCCESS(x)                                                                          \
    {                                                                                                 \
        VkResult result = (x);                                                                        \
        if (result != VK_SUCCESS) {                                                                   \
            spdlog::get("illixr")->debug("[Vulkan] error: {}", ILLIXR::vulkan::error_string(result)); \
            throw std::runtime_error("Vulkan error: " + ILLIXR::vulkan::error_string(result));        \
        }                                                                                             \
    }

#define VK_GET_PROC_ADDR(instance, name) ((PFN_##name) vkGetInstanceProcAddr(instance, #name))
VK_DEFINE_HANDLE(VmaAllocator)

namespace ILLIXR::vulkan {
struct queue_families {
    std::optional<uint32_t> graphics_family;
    std::optional<uint32_t> present_family;
    std::optional<uint32_t> encode_family;
    std::optional<uint32_t> decode_family;
    std::optional<uint32_t> dedicated_transfer;
    std::optional<uint32_t> compute_family;

    [[maybe_unused]] [[nodiscard]] bool has_presentation() const {
        return graphics_family.has_value() && present_family.has_value();
    }

    [[nodiscard]] bool has_compression() const {
        return graphics_family && encode_family.has_value() && decode_family.has_value();
    }
};

struct queue {
    enum queue_type {
        GRAPHICS,
        DEDICATED_TRANSFER,
        PRESENT,
        ENCODE,
        DECODE,
        COMPUTE,
    };

    VkQueue                     vk_queue;
    uint32_t                    family;
    queue_type                  type;
    std::shared_ptr<std::mutex> mutex;
};

struct swapchain_details {
    VkSurfaceCapabilitiesKHR        capabilities{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR>   present_modes;
};

/**
 * @brief Returns the string representation of a VkResult.
 * @param err_code The VkResult to convert to a string.
 * @return The string representation of the VkResult.
 */
std::string error_string(VkResult err_code);

/**
 * @brief Creates a VkShaderModule from SPIR-V bytecode.
 *
 * @param device The Vulkan device to use.
 * @param code The SPIR-V bytecode.
 * @return The created VkShaderModule.
 */
VkShaderModule create_shader_module(VkDevice device, std::vector<char>&& code);

VkSemaphore create_timeline_semaphore(VkDevice device, int initial_value = 0,
                                      VkExportSemaphoreCreateInfo* export_semaphore_create_info = nullptr);

[[maybe_unused]] void wait_timeline_semaphore(VkDevice device, VkSemaphore semaphore, uint64_t value);

void wait_timeline_semaphores(VkDevice device, const std::map<VkSemaphore, uint64_t>& semaphores);

/**
 * @brief Creates a VMA allocator.
 *
 * @param vk_instance The Vulkan instance to use.
 * @param vk_physical_device The Vulkan physical device to use.
 * @param vk_device The Vulkan device to use.
 * @return The created VMA allocator.
 */
VmaAllocator create_vma_allocator(VkInstance vk_instance, VkPhysicalDevice vk_physical_device, VkDevice vk_device);

/**
 * @brief Creates a one-time command buffer.
 *
 * @param vk_device The Vulkan device to use.
 * @param vk_command_pool The Vulkan command pool to use.
 * @return The created command buffer.
 */
VkCommandBuffer begin_one_time_command(VkDevice vk_device, VkCommandPool vk_command_pool);

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
void end_one_time_command(VkDevice vk_device, VkCommandPool vk_command_pool, const queue& q, VkCommandBuffer vk_command_buffer);

/**
 * @brief Creates a VkCommandPool.
 *
 * @param device The Vulkan device to use.
 * @param queue_family_index The queue family index to use.
 * @return The created VkCommandPool.
 */
VkCommandPool create_command_pool(VkDevice device, uint32_t queue_family_index);

/**
 * @brief Creates a VkCommandBuffer.
 *
 * @param device The Vulkan device to use.
 * @param command_pool The Vulkan command pool to use.
 */
VkCommandBuffer create_command_buffer(VkDevice device, VkCommandPool command_pool);

VkResult locked_queue_submit(queue& q, uint32_t submitCount, const VkSubmitInfo* pSubmits, VkFence fence);

/**
 * @brief Reads a file into a vector of chars.
 *
 * @param path The path to the file.
 * @return The vector of chars.
 */
std::vector<char> read_file(const std::string& path);

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
void copy_buffer_to_image(VkDevice vk_device, queue q, VkCommandPool vk_command_pool, VkBuffer buffer, VkImage image,
                          uint32_t width, uint32_t height);

swapchain_details query_swapchain_details(VkPhysicalDevice const& physical_device, VkSurfaceKHR const& vk_surface);

queue_families find_queue_families(VkPhysicalDevice const& physical_device, VkSurfaceKHR const& vk_surface,
                                   bool no_present = false);

VkImageView create_image_view(VkDevice device, VkImage image, VkFormat format, VkImageAspectFlags aspect_flags);
} // namespace ILLIXR::vulkan
