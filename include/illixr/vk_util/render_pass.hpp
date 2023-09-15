#pragma once

#include <vector>
#define GLFW_INCLUDE_VULKAN
#include "../data_format.hpp"
#include "../phonebook.hpp"

#include <GLFW/glfw3.h>

using namespace ILLIXR;

// render_pass defines the interface for a render pass. For now, it is only used for timewarp and app.
class render_pass : public phonebook::service {
public:
    /**
     * @brief Update the uniforms for the render pass.
     *
     * @param render_pose For an app pass, this is the pose to use for rendering. For a timewarp pass, this is the pose
     * previously supplied to the app pass.
     */
    virtual void update_uniforms(const pose_type& render_pose) = 0;

    /**
     * @brief Destroy the render pass and free all Vulkan resources.
     */
    virtual void destroy() = 0;

    ~render_pass() override = default;

    VkPipeline pipeline = VK_NULL_HANDLE;
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
    virtual void setup(VkRenderPass render_pass, uint32_t subpass, std::array<std::vector<VkImageView>, 2> buffer_pool,
                       bool input_texture_vulkan_coordinates) = 0;
    /**
     * @brief Record a command buffer for a given eye.
     *
     * @param commandBuffer The command buffer to record to.
     * @param buffer_ind The index of the buffer to use.
     * @param left Whether to render the left eye or the right eye. True for left eye, false for right eye.
     */
    virtual void record_command_buffer(VkCommandBuffer commandBuffer, int buffer_ind, bool left) = 0;
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
    virtual void setup(VkRenderPass render_pass, uint32_t subpass) = 0;

    /**
     * @brief Record a command buffer for a given eye.
     *
     * @param commandBuffer The command buffer to record to.
     * @param eye The eye to render.
     */
    virtual void record_command_buffer(VkCommandBuffer commandBuffer, int eye) = 0;
};