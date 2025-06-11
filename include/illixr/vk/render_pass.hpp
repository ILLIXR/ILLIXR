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
    virtual void record_command_buffer(VkCommandBuffer commandBuffer, VkFramebuffer framebuffer, int buffer_ind, bool left) = 0;

    /**
     * @brief Update the uniforms for the render pass.
     *
     * @param render_pose For an app pass, this is the pose to use for rendering. For a timewarp pass, this is the pose
     * previously supplied to the app pass.
     */
    virtual void update_uniforms(const data_format::pose_type& render_pose, bool left) {
        (void) render_pose;
    };

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
    void log_pose_to_csv(const time_point& now, const data_format::pose_type& render_pose, const data_format::pose_type& reprojection_pose) {
        if (!pose_csv_logger_) {
            // Initialize the CSV logger if it doesn't exist
            pose_csv_logger_ = spdlog::basic_logger_mt("pose_csv_logger", "logs/pose_data.csv", true);
            
            // Set the pattern to just write the message (no timestamp or log level)
            pose_csv_logger_->set_pattern("%v");
            
            // Write header row
            pose_csv_logger_->info("record_t,render_pose_t,reprojection_pose_t,render_pos_x,render_pos_y,render_pos_z,render_orient_w,render_orient_x,render_orient_y,render_orient_z,reprojection_pos_x,reprojection_pos_y,reprojection_pos_z,reprojection_orient_w,reprojection_orient_x,reprojection_orient_y,reprojection_orient_z");
        }
        
        // Log the pose data in CSV format
        pose_csv_logger_->info("{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{}",
            now.time_since_epoch().count(),
            render_pose.sensor_time.time_since_epoch().count(),
            reprojection_pose.sensor_time.time_since_epoch().count(),
            render_pose.position.x(), render_pose.position.y(), render_pose.position.z(),
            render_pose.orientation.w(), render_pose.orientation.x(), render_pose.orientation.y(), render_pose.orientation.z(),
            reprojection_pose.position.x(), reprojection_pose.position.y(), reprojection_pose.position.z(),
            reprojection_pose.orientation.w(), reprojection_pose.orientation.x(), reprojection_pose.orientation.y(), reprojection_pose.orientation.z()
        );
        
        // Flush to ensure data is written immediately
        pose_csv_logger_->flush();
    }
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
