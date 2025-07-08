#pragma once

#include "illixr/data_format/pose_prediction.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"
#include "illixr/vk/display_provider.hpp"
#include "illixr/vk/render_pass.hpp"
#include "illixr/vk/vulkan_utils.hpp"

#include <functional>
#include <stack>

namespace ILLIXR {
class native_renderer : public threadloop {
public:
    [[maybe_unused]] native_renderer(const std::string& name, phonebook* pb);
    ~native_renderer() override;

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
    void record_src_command_buffer(vulkan::image_index_t buffer_index);

    void record_post_processing_command_buffer(vulkan::image_index_t buffer_index, uint32_t swapchain_image_index_);
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
     */
    void create_depth_image(vulkan::vk_image& depth_image);

    /**
     * @brief Creates an offscreen target for the application to render to.
     * @param image Pointer to the offscreen image handle.
     */
    void create_offscreen_target(vulkan::vk_image& image);

    void create_offscreen_pool();

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

    /**
     * @brief Logs the pose to a CSV file.
     * @param pose The fast pose to log.
     * @param pose_type The type of the pose (e.g., "render", "reprojection").
     */
    void log_pose_to_csv(const ILLIXR::data_format::fast_pose_type& pose, const std::string& pose_type);

    const std::shared_ptr<switchboard>                  switchboard_;
    const std::shared_ptr<spdlog::logger>               log_;
    std::shared_ptr<spdlog::logger>                     nr_pose_csv_logger_;
    const std::shared_ptr<data_format::pose_prediction> pose_prediction_;
    const std::shared_ptr<vulkan::display_provider>     display_sink_;
    const std::shared_ptr<vulkan::timewarp>             timewarp_;
    const std::shared_ptr<vulkan::app>                  app_;
    const std::shared_ptr<const relative_clock>         clock_;

    uint32_t width_      = 0;
    uint32_t height_     = 0;
    bool     export_dma_ = false;

    std::stack<std::function<void()>> deletion_queue_;

    VkCommandPool   command_pool_{};
    VkCommandBuffer app_command_buffer_{};
    VkCommandBuffer timewarp_command_buffer_{};

    std::vector<std::array<vulkan::vk_image, 2>> depth_images_{};
    std::vector<std::array<vulkan::vk_image, 2>> depth_attachment_images_{};

    VkExportMemoryAllocateInfo                offscreen_export_mem_alloc_info_{};
    VmaPoolCreateInfo                         offscreen_pool_create_info_{};
    VmaPool                                   offscreen_pool_{};
    std::vector<std::array<VkFramebuffer, 2>> offscreen_framebuffers_{};

    std::vector<std::array<vulkan::vk_image, 2>> offscreen_images_{};

    std::vector<VkFramebuffer> swapchain_framebuffers_{};

    VkRenderPass app_pass_{};

    VkRenderPass timewarp_pass_{};
    VkSemaphore  image_available_semaphore_{};
    VkSemaphore  app_render_finished_semaphore_{};
    VkSemaphore  timewarp_render_finished_semaphore_{};

    VkFence frame_fence_{};

    uint32_t swapchain_image_index_    = UINT32_MAX; // set to UINT32_MAX after present
    uint64_t timeline_semaphore_value_ = 1;

    int                                                         fps_{};
    switchboard::reader<switchboard::event_wrapper<time_point>> vsync_;
    time_point                                                  last_fps_update_;

    std::shared_ptr<vulkan::buffer_pool<data_format::fast_pose_type>> buffer_pool_;
};
} // namespace ILLIXR
