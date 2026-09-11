/// @file frame_saver_integration.hpp
/// @brief Example integration of frame_saver into offload_rendering_server
///
/// Add this code to your offload_rendering_server to save frames.
///
/// SETUP:
/// 1. Copy frame_saver.hpp to your include directory
/// 2. Download stb_image_write.h from https://github.com/nothings/stb
/// 3. Add the includes and member variable shown below
/// 4. Call save_frame() after encoding

#pragma once
#ifdef DUMP_FRAMES
// ============================================================================
// In offload_rendering_server.hpp - Add these includes:
// ============================================================================
/*
#define STB_IMAGE_WRITE_IMPLEMENTATION  // Only in ONE cpp file
#include "frame_saver.hpp"
*/

// ============================================================================
// In offload_rendering_server class - Add member variable:
// ============================================================================
/*
private:
    std::unique_ptr<ILLIXR::frame_saver> frame_saver_;
*/

// ============================================================================
// In constructor or start() - Initialize the frame saver:
// ============================================================================
/*
void init_frame_saver() {
    ILLIXR::frame_saver_config config;
    config.output_directory = "server_frames";
    config.save_interval = 10;  // Save every 10th frame
    config.prefix = "server";
    config.enabled = true;

    // Can also check environment variable
    if (const char* env = std::getenv("ILLIXR_SAVE_FRAMES")) {
        config.enabled = (std::string(env) == "1");
    }
    if (const char* env = std::getenv("ILLIXR_SAVE_INTERVAL")) {
        config.save_interval = std::stoi(env);
    }

    frame_saver_ = std::make_unique<ILLIXR::frame_saver>(config);
    spdlog::get("illixr")->info("Frame saver initialized: enabled={}, interval={}",
                                 config.enabled, config.save_interval);
}
*/

// ============================================================================
// After encoding - Save the frame:
// ============================================================================
/*
void save_encoded_frame(int eye, const vulkan_image_info& vk_image) {
    if (!frame_saver_ || !frame_saver_->will_save_next()) {
        return;
    }

    // Option 1: Save the raw Vulkan image (need to read back from GPU)
    // This requires a staging buffer and vkCmdCopyImageToBuffer

    // Option 2: Save the encoded bitstream
    // frame_saver_->save_bitstream(encoded_data.data(), encoded_data.size(), eye, "hevc");

    // Option 3: If you have CPU-accessible pixel data, save directly
    // frame_saver_->save_bgra(pixel_data, width, height, eye, "color");
}
*/

// ============================================================================
// Complete example function to read back Vulkan image and save as PNG:
// ============================================================================

#    include <memory>
#    include <vector>
#    include <vulkan/vulkan.h>

namespace ILLIXR {

/// Helper to read back a Vulkan image to CPU memory and save it
/// This is expensive and should only be used for debugging
class vulkan_frame_readback {
public:
    vulkan_frame_readback(VkDevice device, VkPhysicalDevice physical_device, VkCommandPool cmd_pool, VkQueue queue)
        : device_(device)
        , physical_device_(physical_device)
        , cmd_pool_(cmd_pool)
        , queue_(queue)
        , staging_buffer_(VK_NULL_HANDLE)
        , staging_memory_(VK_NULL_HANDLE)
        , staging_size_(0) { }

    ~vulkan_frame_readback() {
        cleanup();
    }

    /// Read back an image to CPU memory
    /// @param image Source VkImage (must be in TRANSFER_SRC_OPTIMAL or GENERAL layout)
    /// @param width Image width
    /// @param height Image height
    /// @param format Image format (must be BGRA or RGBA 8-bit)
    /// @return Vector of pixel data (BGRA format)
    std::vector<uint8_t> readback(VkImage image, uint32_t width, uint32_t height, VkFormat format) {
        size_t pixel_size    = 4; // Assuming 4 bytes per pixel (BGRA/RGBA)
        size_t required_size = width * height * pixel_size;

        // Create or resize staging buffer
        if (required_size > staging_size_) {
            cleanup();
            create_staging_buffer(required_size);
        }

        // Create command buffer
        VkCommandBufferAllocateInfo alloc_info = {};
        alloc_info.sType                       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc_info.commandPool                 = cmd_pool_;
        alloc_info.level                       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc_info.commandBufferCount          = 1;

        VkCommandBuffer cmd;
        vkAllocateCommandBuffers(device_, &alloc_info, &cmd);

        VkCommandBufferBeginInfo begin_info = {};
        begin_info.sType                    = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags                    = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &begin_info);

        // Copy image to buffer
        VkBufferImageCopy region               = {};
        region.bufferOffset                    = 0;
        region.bufferRowLength                 = 0;
        region.bufferImageHeight               = 0;
        region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel       = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount     = 1;
        region.imageOffset                     = {0, 0, 0};
        region.imageExtent                     = {width, height, 1};

        vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging_buffer_, 1, &region);

        vkEndCommandBuffer(cmd);

        // Submit and wait
        VkSubmitInfo submit       = {};
        submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers    = &cmd;

        vkQueueSubmit(queue_, 1, &submit, VK_NULL_HANDLE);
        vkQueueWaitIdle(queue_);

        vkFreeCommandBuffers(device_, cmd_pool_, 1, &cmd);

        // Map and copy data
        void* mapped;
        vkMapMemory(device_, staging_memory_, 0, required_size, 0, &mapped);

        std::vector<uint8_t> result(required_size);
        memcpy(result.data(), mapped, required_size);

        vkUnmapMemory(device_, staging_memory_);

        return result;
    }

private:
    void create_staging_buffer(size_t size) {
        VkBufferCreateInfo buffer_info = {};
        buffer_info.sType              = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_info.size               = size;
        buffer_info.usage              = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        buffer_info.sharingMode        = VK_SHARING_MODE_EXCLUSIVE;

        vkCreateBuffer(device_, &buffer_info, nullptr, &staging_buffer_);

        VkMemoryRequirements mem_reqs;
        vkGetBufferMemoryRequirements(device_, staging_buffer_, &mem_reqs);

        VkPhysicalDeviceMemoryProperties mem_props;
        vkGetPhysicalDeviceMemoryProperties(physical_device_, &mem_props);

        uint32_t memory_type = UINT32_MAX;
        for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
            if ((mem_reqs.memoryTypeBits & (1 << i)) &&
                (mem_props.memoryTypes[i].propertyFlags &
                 (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))) {
                memory_type = i;
                break;
            }
        }

        VkMemoryAllocateInfo alloc_info = {};
        alloc_info.sType                = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.allocationSize       = mem_reqs.size;
        alloc_info.memoryTypeIndex      = memory_type;

        vkAllocateMemory(device_, &alloc_info, nullptr, &staging_memory_);
        vkBindBufferMemory(device_, staging_buffer_, staging_memory_, 0);

        staging_size_ = size;
    }

    void cleanup() {
        if (staging_buffer_ != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_, staging_buffer_, nullptr);
            staging_buffer_ = VK_NULL_HANDLE;
        }
        if (staging_memory_ != VK_NULL_HANDLE) {
            vkFreeMemory(device_, staging_memory_, nullptr);
            staging_memory_ = VK_NULL_HANDLE;
        }
        staging_size_ = 0;
    }

    VkDevice         device_;
    VkPhysicalDevice physical_device_;
    VkCommandPool    cmd_pool_;
    VkQueue          queue_;
    VkBuffer         staging_buffer_;
    VkDeviceMemory   staging_memory_;
    size_t           staging_size_;
};

} // namespace ILLIXR
#endif