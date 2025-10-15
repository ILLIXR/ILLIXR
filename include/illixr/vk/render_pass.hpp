#pragma once

#define GLFW_INCLUDE_VULKAN
#if defined(_WIN32) || defined(_WIN64)
    #include <windows.h>
#endif
#include "illixr/data_format/misc.hpp"
#include "illixr/data_format/pose.hpp"
#include "illixr/phonebook.hpp"
#include "vulkan_objects.hpp"

#include <GLFW/glfw3.h>
#include <vector>

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
    virtual void update_uniforms(const data_format::pose_type& render_pose) {
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
