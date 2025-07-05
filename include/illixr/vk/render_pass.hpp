#pragma once

#define GLFW_INCLUDE_VULKAN
#include "illixr/data_format/misc.hpp"
#include "illixr/data_format/pose.hpp"
#include "illixr/phonebook.hpp"
#include "vulkan_objects.hpp"

#include <GLFW/glfw3.h>
#include <vector>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <filesystem>

namespace ILLIXR::vulkan {

// render_pass defines the interface for a render pass. For now, it is only used for timewarp and
// app.
class render_pass : public phonebook::service {
public:
    /**
     * @brief Record a command buffer for a given eye.
     *
     * @param commandBuffer The command buffer to record to.
     * @param buffer_ind The index of the buffer to use.
     * @param left 0 for left eye, 1 for right eye.
     */
    virtual void record_command_buffer(VkCommandBuffer commandBuffer, VkFramebuffer framebuffer, int buffer_ind, bool left, VkFence fence) = 0;

    /**
     * @brief Update the uniforms for the render pass.
     *
     * @param render_pose For an app pass, this is the pose to use for rendering. For a timewarp pass, this is the pose
     * previously supplied to the app pass.
     */
    virtual void update_uniforms(const data_format::fast_pose_type& render_pose, bool left) {
        (void) render_pose;
    };

    virtual void save_frame(VkFence fence) = 0;

    /**
     * @brief Destroy the render pass and free all Vulkan resources.
     */
    virtual void destroy() { };

    virtual bool is_external() = 0;

    ~render_pass() override = default;

    VkPipeline pipeline_ = VK_NULL_HANDLE;
};

// timewarp defines the interface for a warping render pass as a service.
class timewarp : public render_pass {
public:
    /**
     * @brief Setup the timewarp render pass and initailize required Vulkan resources.
     *
     * @param render_pass The render pass to use.
     * @param subpass The subpass to use.
     * @param buffer_pool The buffer pool to use.
     * @param input_texture_vulkan_coordinates Whether the input texture is in Vulkan coordinates.
     */
    virtual void setup(VkRenderPass render_pass, uint32_t subpass,
                       std::shared_ptr<buffer_pool<data_format::fast_pose_type>> buffer_pool,
                       bool                                                      input_texture_vulkan_coordinates) = 0;
protected:
    // CSV logger for pose data
    std::shared_ptr<spdlog::logger> pose_csv_logger_ = nullptr;
    
    /**
     * @brief Logs the render pose and reprojection pose to a CSV file.
     *
     * @param render_pose The pose used for rendering the frame
     * @param reprojection_pose The pose used for reprojection/timewarp
     */
    void log_pose_to_csv(const time_point& now, const data_format::fast_pose_type& render_pose, const data_format::fast_pose_type& reprojection_pose) {
        if (!pose_csv_logger_) {
            // Initialize the CSV logger if it doesn't exist
            pose_csv_logger_ = spdlog::basic_logger_mt("pose_csv_logger", "logs/pose_data.csv", true);
            
            // Set the pattern to just write the message (no timestamp or log level)
            pose_csv_logger_->set_pattern("%v");
            
            // Write header row
            pose_csv_logger_->info("record_t,render_pose_cam_t,render_pose_imu_t,render_pose_target_t,reprojection_pose_cam_t,reprojection_pose_imu_t,reprojection_pose_target_t,render_pos_x,render_pos_y,render_pos_z,render_orient_w,render_orient_x,render_orient_y,render_orient_z,reprojection_pos_x,reprojection_pos_y,reprojection_pos_z,reprojection_orient_w,reprojection_orient_x,reprojection_orient_y,reprojection_orient_z");
        }
        
        // Log the pose data in CSV format
        pose_csv_logger_->info("{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{}",
            now.time_since_epoch().count(),
            render_pose.pose.cam_time.time_since_epoch().count(),
            render_pose.pose.imu_time.time_since_epoch().count(),
            render_pose.predict_target_time.time_since_epoch().count(),
            reprojection_pose.pose.cam_time.time_since_epoch().count(),
            reprojection_pose.pose.imu_time.time_since_epoch().count(),
            reprojection_pose.predict_target_time.time_since_epoch().count(),
            render_pose.pose.position.x(), render_pose.pose.position.y(), render_pose.pose.position.z(),
            render_pose.pose.orientation.w(), render_pose.pose.orientation.x(), render_pose.pose.orientation.y(), render_pose.pose.orientation.z(),
            reprojection_pose.pose.position.x(), reprojection_pose.pose.position.y(), reprojection_pose.pose.position.z(),
            reprojection_pose.pose.orientation.w(), reprojection_pose.pose.orientation.x(), reprojection_pose.pose.orientation.y(), reprojection_pose.pose.orientation.z()
        );
        
        // Flush to ensure data is written immediately
        pose_csv_logger_->flush();
    }
/*
    void save_frame(const VkImage& image, uint32_t width, uint32_t height, uint32_t frame_number, bool left, 
                                  const std::string& output_directory) {
    
    // Ensure the output directory exists - I do not like this checking as it executes every time a frame is saved
    std::filesystem::path output_directory_ = std::filesystem::current_path() / output_directory;
    if (!std::filesystem::exists(output_directory_)) {
        std::filesystem::create_directories(output_directory_);
        spdlog::get("illixr")->info("[frame_logger] Created output directory: {}", output_directory_.string());
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

    // Submit command buffer
    vulkan::end_one_time_command(display_provider_->vk_device_,
                                display_provider_->command_pool_,
                                display_provider_->queues_[vulkan::queue::GRAPHICS],
                                command_buffer);

    // Map buffer and save image
    void* data;
    vmaMapMemory(display_provider_->vma_allocator_, staging_buffer_allocation, &data);

    std::string filename = (output_directory_ / ("frame_" + std::to_string(frame_number) + (left ? "_left" : "_right") + ".png")).string();
    stbi_write_png(filename.c_str(), width, height, 4, data, width * 4);

    vmaUnmapMemory(display_provider_->vma_allocator_, staging_buffer_allocation);

    // Cleanup
    vmaDestroyBuffer(display_provider_->vma_allocator_, staging_buffer, staging_buffer_allocation);

    spdlog::get("illixr")->debug("[frame_logger] Saved frame {} to {}", frame_number, filename);
}
*/
};

// app defines the interface for an application render pass as a service.
class app : public render_pass {
public:
    /**
     * @brief Setup the app render pass and initailize required Vulkan resources.
     *
     * @param render_pass The render pass to use.
     * @param subpass The subpass to use.
     */
    virtual void setup(VkRenderPass render_pass, uint32_t subpass,
                       std::shared_ptr<buffer_pool<data_format::fast_pose_type>> buffer_pool) = 0;
};
} // namespace ILLIXR::vulkan
