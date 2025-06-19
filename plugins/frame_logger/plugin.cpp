#include "plugin.hpp"
#include "illixr/error_util.hpp"
#include <spdlog/spdlog.h>
#include <stb_image_write.h>
#include <vulkan/vulkan.h>

namespace ILLIXR {

frame_logger::frame_logger(std::string name, phonebook* pb)
    : plugin{name, pb}
    , display_provider_{pb->lookup_impl<vulkan::display_provider>()}
    , switchboard_{pb->lookup_impl<switchboard>()}
    , frame_reader_{switchboard_->get_reader<switchboard::event_wrapper<time_point>>("frame")} {
    create_output_directory();
}
    
frame_logger::~frame_logger() {
    stop();
}

void frame_logger::start() {
    is_running_ = true;
    spdlog::get("illixr")->info("[frame_logger] Started frame logging");
}

void frame_logger::stop() {
    is_running_ = false;
    spdlog::get("illixr")->info("[frame_logger] Stopped frame logging");
}

void frame_logger::create_output_directory() {
    output_directory_ = std::filesystem::current_path() / "frame_logs";
    if (!std::filesystem::exists(output_directory_)) {
        std::filesystem::create_directories(output_directory_);
    }
    spdlog::get("illixr")->info("[frame_logger] Created output directory: {}", output_directory_.string());
}

void frame_logger::handle_frame(const switchboard::event_wrapper<time_point>& event) {
    if (!is_running_) {
        return;
    }

    // Get the current swapchain image
    uint32_t image_index = 0;
    VkResult result = vkAcquireNextImageKHR(display_provider_->vk_device_,
                                           display_provider_->vk_swapchain_,
                                           UINT64_MAX,
                                           VK_NULL_HANDLE,
                                           VK_NULL_HANDLE,
                                           &image_index);

    if (result == VK_SUCCESS) {
        save_frame(display_provider_->swapchain_images_[image_index],
                  display_provider_->swapchain_extent_.width,
                  display_provider_->swapchain_extent_.height,
                  frame_counter_++);
    }
}

void frame_logger::save_frame(const VkImage& image, uint32_t width, uint32_t height, uint32_t frame_number) {
    if (!is_running_) {
        return;
    }

    // Create staging buffer
    VkBuffer staging_buffer;
    VmaAllocation staging_buffer_allocation;
    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = width * height * 4; // RGBA format
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_CPU_ONLY;

    VK_ASSERT_SUCCESS(vmaCreateBuffer(display_provider_->vma_allocator_, &buffer_info, &alloc_info,
                                    &staging_buffer, &staging_buffer_allocation, nullptr));

    // Create command buffer for copy operation
    VkCommandBuffer command_buffer = vulkan::begin_one_time_command(
        display_provider_->vk_device_,
        display_provider_->command_pool_
    );

    // Transition image layout for copying
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    vkCmdPipelineBarrier(command_buffer,
                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT,
                        0,
                        0, nullptr,
                        0, nullptr,
                        1, &barrier);

    // Copy image to buffer
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};

    vkCmdCopyImageToBuffer(command_buffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                          staging_buffer, 1, &region);

    // Transition image back to present layout
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = 0;

    vkCmdPipelineBarrier(command_buffer,
                        VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                        0,
                        0, nullptr,
                        0, nullptr,
                        1, &barrier);

    // Submit command buffer
    vulkan::end_one_time_command(display_provider_->vk_device_,
                                display_provider_->command_pool_,
                                display_provider_->queues_[vulkan::queue::GRAPHICS],
                                command_buffer);

    // Map buffer and save image
    void* data;
    vmaMapMemory(display_provider_->vma_allocator_, staging_buffer_allocation, &data);

    std::string filename = (output_directory_ / ("frame_" + std::to_string(frame_number) + ".png")).string();
    stbi_write_png(filename.c_str(), width, height, 4, data, width * 4);

    vmaUnmapMemory(display_provider_->vma_allocator_, staging_buffer_allocation);

    // Cleanup
    vmaDestroyBuffer(display_provider_->vma_allocator_, staging_buffer, staging_buffer_allocation);

    spdlog::get("illixr")->debug("[frame_logger] Saved frame {} to {}", frame_number, filename);
}

} // namespace ILLIXR 