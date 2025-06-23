#include "plugin.hpp"
#include "illixr/error_util.hpp"
#include <vulkan/vulkan.h>
#include "illixr/vk/vulkan_utils.hpp"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "illixr/gl_util/lib/stb_image_write.h"


namespace ILLIXR {

frame_logger::frame_logger(std::string name, phonebook* pb)
    : threadloop{name, pb}
    , phonebook_{pb}
    , switchboard_{pb->lookup_impl<switchboard>()}
    , log_{spdlogger("info")}
    , frame_reader_{switchboard_->get_buffered_reader<data_format::frame_to_be_saved>("frames_to_be_saved")}
    { }

void frame_logger::start() {
    threadloop::start();
    is_running_ = true;
    log_->info("Started frame logging");
}

void frame_logger::stop() {
    is_running_ = false;
    log_->info("Stopped frame logging");
}

void frame_logger::_p_thread_setup() {
    // Wait for display provider to be ready
    while (display_provider_ == nullptr) {
        try {
            display_provider_ = phonebook_->lookup_impl<vulkan::display_provider>();
        } catch (const std::exception& e) {
            log_->info("Display provider not ready yet");
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    log_->info("Display provider obtained");

    if (display_provider_->vma_allocator_) {
        this->vma_allocator_ = display_provider_->vma_allocator_;
        log_->info("Using existing VMA allocator from display provider");
    } else {
        this->vma_allocator_ = vulkan::create_vma_allocator(
            display_provider_->vk_instance_, display_provider_->vk_physical_device_, display_provider_->vk_device_);
        log_->info("Created new VMA allocator");

        deletion_queue_.emplace([=]() {
            vmaDestroyAllocator(vma_allocator_);
        });
    }

    command_pool_ =
        vulkan::create_command_pool(display_provider_->vk_device_, display_provider_->queues_[vulkan::queue::GRAPHICS].family);
    log_->info("Created command pool");
    deletion_queue_.emplace([=]() {
        vkDestroyCommandPool(display_provider_->vk_device_, command_pool_, nullptr);
    });
}

void frame_logger::_p_one_iteration() {
    if (!is_running_) {
        return;
    }

    log_->info("_p_one_iteration called");

    // Process frames to be saved
    auto frame_event = frame_reader_.size() > 0 ? frame_reader_.dequeue() : nullptr;
    if (frame_event) {
        const auto& frame = *frame_event;
        log_->info("Processing frame {}", frame.frame_number);
        save_frame(frame.image, frame.width, frame.height, frame.frame_number, frame.left, frame.output_directory);
    } 
}

void frame_logger::save_frame(const VkImage& image, uint32_t width, uint32_t height, uint32_t frame_number, bool left, 
                                  const std::string& output_directory) {
    
    // Ensure the output directory exists - I do not like this checking as it executes every time a frame is saved
    std::filesystem::path output_directory_ = std::filesystem::current_path() / output_directory;
    if (!std::filesystem::exists(output_directory_)) {
        std::filesystem::create_directories(output_directory_);
        log_->info("Created output directory: {}", output_directory_.string());
    }

    // Create staging buffer
    VkBuffer staging_buffer;
    VmaAllocation staging_buffer_allocation;
    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = width * height * 4; // RGBA format - wrong! Monado uses GBRA
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_CPU_ONLY;

    VK_ASSERT_SUCCESS(vmaCreateBuffer(this->vma_allocator_, &buffer_info, &alloc_info,
                                    &staging_buffer, &staging_buffer_allocation, nullptr));
    log_->info("Creating staging buffer (vmaCreateBuffer) for frame {}", frame_number);

    // Create command buffer for copy operation
    VkCommandBuffer command_buffer = vulkan::begin_one_time_command(
        display_provider_->vk_device_,
        this->command_pool_
    );

    // Transition image layout for copying
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    // barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; // this may not be right for source images and offscreen images
    barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; // This is the layout for the src image
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT; // Assuming the src image was used as a shader input
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    vkCmdPipelineBarrier(command_buffer,
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT,
                        0,
                        0, nullptr,
                        0, nullptr,
                        1, &barrier);
    log_->info("Transition image layout for copying (vkCmdPipelineBarrier) for frame {}", frame_number);

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
    log_->info("Copy image to buffer (vkCmdCopyImageToBuffer) for frame {}", frame_number);

    // Transition image back to present layout
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(command_buffer,
                        VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                        0,
                        0, nullptr,
                        0, nullptr,
                        1, &barrier);
    log_->info("Transition image back to present layout (vkCmdPipelineBarrier) for frame {}", frame_number);

    // Submit command buffer
    vulkan::end_one_time_command(display_provider_->vk_device_,
                                this->command_pool_,
                                display_provider_->queues_[vulkan::queue::GRAPHICS],
                                command_buffer);

    // Map buffer and save image
    void* data;
    vmaMapMemory(this->vma_allocator_, staging_buffer_allocation, &data);

    // Convert to byte pointer
    uint8_t* pixels = static_cast<uint8_t*>(data);

    // Swap R and B channels (BGRA → RGBA)
    for (uint32_t i = 0; i < width * height; ++i) {
        std::swap(pixels[i * 4 + 0], pixels[i * 4 + 2]); // B <-> R
    }

    std::string filename = (output_directory_ / ("frame_" + std::to_string(frame_number) + (left ? "_left" : "_right") + ".png")).string();
    stbi_write_png(filename.c_str(), width, height, 4, pixels, width * 4);

    vmaUnmapMemory(this->vma_allocator_, staging_buffer_allocation);

    // Cleanup
    vmaDestroyBuffer(this->vma_allocator_, staging_buffer, staging_buffer_allocation);

    log_->info("Saved frame {} to {}", frame_number, filename);
}

} // namespace ILLIXR
PLUGIN_MAIN(frame_logger)
