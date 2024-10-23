#pragma once

#include "illixr/phonebook.hpp"
#include "illixr/pose_prediction.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"
#include "illixr/vk_util/display_sink.hpp"
#include "illixr/vk_util/render_pass.hpp"

namespace ILLIXR {
class native_renderer : public threadloop {
public:
    [[maybe_unused]] native_renderer(const std::string& name, phonebook* pb);

    /**
     * @brief Sets up the thread for the plugin.
     *
     * This function initializes depth images, offscreen targets, command buffers, sync objects,
     * application and timewarp passes, offscreen and swapchain framebuffers. Then, it initializes
     * application and timewarp with their respective passes.
     */
    void _p_thread_setup() override;

    /**
     * @brief Executes one iteration of the plugin's main loop.
     *
     * This function handles window events, acquires the next image from the swapchain, updates uniforms,
     * records command buffers, submits commands to the graphics queue, and presents the rendered image.
     * It also handles swapchain recreation if necessary and updates the frames per second (FPS) counter.
     *
     * @throws runtime_error If any Vulkan operation fails.
     */
    void _p_one_iteration() override;

private:
    /**
     * @brief Recreates the Vulkan swapchain.
     *
     * This function waits for the device to be idle, destroys the existing swapchain framebuffers,
     * recreates the swapchain, and then creates new swapchain framebuffers.
     *
     * @throws runtime_error If any Vulkan operation fails.
     */
    [[maybe_unused]] void recreate_swapchain();

    /**
     * @brief Creates framebuffers for each swapchain image view.
     *
     * @throws runtime_error If framebuffer creation fails.
     */
    void create_swapchain_framebuffers();

    /**
     * @brief Records the command buffer for a single frame.
     * @param swapchain_image_index The index of the swapchain image to render to.
     */
    void record_command_buffer(uint32_t swapchain_image_index);

    /**
     * @brief Creates synchronization objects for the application.
     *
     * This function creates a timeline semaphore for the application render finished signal,
     * a binary semaphore for the image available signal, a binary semaphore for the timewarp render finished signal,
     * and a fence for frame synchronization.
     *
     * @throws runtime_error If any Vulkan operation fails.
     */
    void create_sync_objects();

    /**
     * @brief Creates a depth image for the application.
     * @param depth_image Pointer to the depth image handle.
     * @param depth_image_allocation Pointer to the depth image memory allocation handle.
     * @param depth_image_view Pointer to the depth image view handle.
     */
    void create_depth_image(VkImage* depth_image, VmaAllocation* depth_image_allocation, VkImageView* depth_image_view);

    /**
     * @brief Creates an offscreen target for the application to render to.
     * @param offscreen_image Pointer to the offscreen image handle.
     * @param offscreen_image_allocation Pointer to the offscreen image memory allocation handle.
     * @param offscreen_image_view Pointer to the offscreen image view handle.
     * @param offscreen_framebuffer Pointer to the offscreen framebuffer handle.
     */
    void create_offscreen_target(VkImage* offscreen_image, VmaAllocation* offscreen_image_allocation,
                                 VkImageView* offscreen_image_view, [[maybe_unused]] VkFramebuffer* offscreen_framebuffer);

    /**
     * @brief Creates the offscreen framebuffers for the application.
     */
    void create_offscreen_framebuffers();

    /**
     * @brief Creates a render pass for the application.
     *
     * This function sets up the attachment descriptions for color and depth, the attachment references,
     * the subpass description, and the subpass dependencies. It then creates a render pass with these configurations.
     *
     * @throws runtime_error If render pass creation fails.
     */
    void create_app_pass();

    /**
     * @brief Creates a render pass for timewarp.
     */
    void create_timewarp_pass();

    const std::shared_ptr<switchboard>          switchboard_;
    const std::shared_ptr<pose_prediction>      pose_prediction_;
    const std::shared_ptr<display_sink>         display_sink_;
    const std::shared_ptr<timewarp>             timewarp_;
    const std::shared_ptr<app>                  app_;
    const std::shared_ptr<const relative_clock> clock_;

    VkCommandPool   command_pool_{};
    VkCommandBuffer app_command_buffer_{};
    VkCommandBuffer timewarp_command_buffer_{};

    std::array<VkImage, 2>       depth_images_{};
    std::array<VmaAllocation, 2> depth_image_allocations_{};
    std::array<VkImageView, 2>   depth_image_views_{};

    std::array<VkImage, 2>       offscreen_images_{};
    std::array<VmaAllocation, 2> offscreen_image_allocations_{};
    std::array<VkImageView, 2>   offscreen_image_views_{};
    std::array<VkFramebuffer, 2> offscreen_framebuffers_{};

    std::vector<VkFramebuffer> swapchain_framebuffers_;

    VkRenderPass app_pass_{};
    VkRenderPass timewarp_pass_{};

    VkSemaphore image_available_semaphore_{};
    VkSemaphore app_render_finished_semaphore_{};
    VkSemaphore timewarp_render_finished_semaphore_{};
    VkFence     frame_fence_{};

    uint64_t timeline_semaphore_value_ = 1;

    int        fps_{};
    time_point last_fps_update_;
};
} // namespace ILLIXR