#include "plugin.hpp"

#include "illixr/global_module_defs.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <iostream>
#include <vector>
#include <vulkan/vulkan.h>

#define TINYOBJLOADER_IMPLEMENTATION
#include "illixr/gl_util/lib/tiny_obj_loader.h"

#define NATIVE_RENDERER_BUFFER_POOL_SIZE 3

using namespace ILLIXR;
using namespace ILLIXR::data_format;

[[maybe_unused]] native_renderer::native_renderer(const std::string& name, phonebook* pb)
    : threadloop{name, pb}
    , switchboard_{phonebook_->lookup_impl<switchboard>()}
    , log_(spdlogger(switchboard_->get_env_char("NATIVE_RENDERER_LOG_LEVEL")))
    , pose_prediction_{phonebook_->lookup_impl<pose_prediction>()}
    , display_sink_{phonebook_->lookup_impl<vulkan::display_provider>()}
    , timewarp_{phonebook_->lookup_impl<vulkan::timewarp>()}
    , app_{phonebook_->lookup_impl<vulkan::app>()}
    , clock_{phonebook_->lookup_impl<relative_clock>()}
    , vsync_{switchboard_->get_reader<switchboard::event_wrapper<time_point>>("vsync_estimate")}
    , last_fps_update_{std::chrono::duration<long, std::nano>{0}} {
    if (switchboard_->get_env_char("ILLIXR_WIDTH") == nullptr || switchboard_->get_env_char("ILLIXR_HEIGHT") == nullptr) {
        log_->warn("Please define ILLIXR_WIDTH and ILLIXR_HEIGHT. Default values used.");
        width_  = display_sink_->swapchain_extent_.width;
        height_ = display_sink_->swapchain_extent_.height;
    } else {
        width_  = std::stoi(switchboard_->get_env_char("ILLIXR_WIDTH"));
        height_ = std::stoi(switchboard_->get_env_char("ILLIXR_HEIGHT"));
    }

    export_dma_ = switchboard_->get_env_bool("ILLIXR_EXPORT_DMA");
    
    // Set up CSV logger for pose data
    spdlog::file_event_handlers handlers;
    handlers.after_open = [](spdlog::filename_t, std::FILE* fstream) {
        fputs("timestamp,pose_type,pos_x,pos_y,pos_z,quat_w,quat_x,quat_y,quat_z,predict_computed_time,predict_target_time\n", fstream);
    };

    nr_pose_csv_logger_ = spdlog::basic_logger_mt(
        "nr_pose_csv_logger_",
        "logs/nr_pose_data.csv",
        true, // Truncate
        handlers
    );
    nr_pose_csv_logger_->set_pattern("%v"); // Only output the message part, no timestamps or logger name
    nr_pose_csv_logger_->set_level(spdlog::level::info);
}

native_renderer::~native_renderer() {
    vkDeviceWaitIdle(display_sink_->vk_device_);

    app_->destroy();
    timewarp_->destroy();

    for (auto& framebuffer : swapchain_framebuffers_) {
        vkDestroyFramebuffer(display_sink_->vk_device_, framebuffer, nullptr);
    }

    // drain deletion_queue_
    while (!deletion_queue_.empty()) {
        deletion_queue_.top()();
        deletion_queue_.pop();
    }
}

/**
 * @brief Logs a pose to the CSV file with the specified pose type.
 * 
 * @param pose The pose to log
 * @param pose_type A string indicating the type of pose (e.g., "render", "reprojection")
 */
void native_renderer::log_pose_to_csv(const fast_pose_type& pose, const std::string& pose_type) {
    if (!nr_pose_csv_logger_) {
        log_->warn("Pose CSV logger is not initialized");
        return;
    }
    
    // Get current timestamp
    auto now = clock_->now().time_since_epoch().count();
    
    // Extract position and orientation
    float pos_x = pose.pose.position.x();
    float pos_y = pose.pose.position.y();
    float pos_z = pose.pose.position.z();
    
    float quat_w = pose.pose.orientation.w();
    float quat_x = pose.pose.orientation.x();
    float quat_y = pose.pose.orientation.y();
    float quat_z = pose.pose.orientation.z();
    
    // Get the prediction timestamps
    auto predict_computed = pose.predict_computed_time.time_since_epoch().count();
    auto predict_target = pose.predict_target_time.time_since_epoch().count();
    
    // Log to CSV
    nr_pose_csv_logger_->info("{},{},{:.6f},{:.6f},{:.6f},{:.6f},{:.6f},{:.6f},{:.6f},{},{}",
                         now, pose_type, pos_x, pos_y, pos_z, quat_w, quat_x, quat_y, quat_z,
                         predict_computed, predict_target);
}

/**
 * @brief Sets up the thread for the plugin.
 *
 * This function initializes depth images, offscreen targets, command buffers, sync objects,
 * application and timewarp passes, offscreen and swapchain framebuffers. Then, it initializes
 * application and timewarp with their respective passes.
 */
void native_renderer::_p_thread_setup() {
    depth_images_.resize(NATIVE_RENDERER_BUFFER_POOL_SIZE);
    offscreen_images_.resize(NATIVE_RENDERER_BUFFER_POOL_SIZE);
    depth_attachment_images_.resize(NATIVE_RENDERER_BUFFER_POOL_SIZE);

    if (timewarp_->is_external() || app_->is_external()) {
        create_offscreen_pool();
    }
    for (auto i = 0; i < NATIVE_RENDERER_BUFFER_POOL_SIZE; i++) {
        for (auto eye = 0; eye < 2; eye++) {
            create_offscreen_target(offscreen_images_[i][eye]);
            create_offscreen_target(depth_images_[i][eye]);
            create_depth_image(depth_attachment_images_[i][eye]);
        }
    }
    this->buffer_pool_ = std::make_shared<vulkan::buffer_pool<fast_pose_type>>(offscreen_images_, depth_images_);

    command_pool_ =
        vulkan::create_command_pool(display_sink_->vk_device_, display_sink_->queues_[vulkan::queue::GRAPHICS].family);
    app_command_buffer_      = vulkan::create_command_buffer(display_sink_->vk_device_, command_pool_);
    timewarp_command_buffer_ = vulkan::create_command_buffer(display_sink_->vk_device_, command_pool_);
    deletion_queue_.emplace([=]() {
        vkDestroyCommandPool(display_sink_->vk_device_, command_pool_, nullptr);
    });

    create_sync_objects();
    if (!app_->is_external()) {
        create_app_pass();
    }
    if (!timewarp_->is_external()) {
        create_timewarp_pass();
    }
    if (!app_->is_external()) {
        create_offscreen_framebuffers();
    }
    create_swapchain_framebuffers();
    app_->setup(app_pass_, 0, buffer_pool_);
    timewarp_->setup(timewarp_pass_, 0, buffer_pool_, app_->is_external());
}

void native_renderer::_p_one_iteration() {
    if (!timewarp_->is_external()) {
        display_sink_->poll_window_events();

        if (swapchain_image_index_ == UINT32_MAX) {
            // Acquire the next image from the swapchain
            auto ret = (vkAcquireNextImageKHR(display_sink_->vk_device_, display_sink_->vk_swapchain_, UINT64_MAX,
                                              image_available_semaphore_, VK_NULL_HANDLE, &swapchain_image_index_));

            // Check if the swapchain is out of date or suboptimal
            if (ret == VK_ERROR_OUT_OF_DATE_KHR || ret == VK_SUBOPTIMAL_KHR) {
                display_sink_->recreate_swapchain();
                return;
            } else {
                VK_ASSERT_SUCCESS(ret)
            }
        }
        VK_ASSERT_SUCCESS(vkResetFences(display_sink_->vk_device_, 1, &frame_fence_))
    }

    auto fast_pose = pose_prediction_->get_fast_pose();
    
    // Log the render pose
    log_pose_to_csv(fast_pose, "render");
    
    if (!app_->is_external()) {
        // Get the current fast pose and update the uniforms
        // log_->debug("Updating uniforms");
        app_->update_uniforms(fast_pose, 1);

        VK_ASSERT_SUCCESS(vkResetCommandBuffer(app_command_buffer_, 0))

        vulkan::image_index_t buffer_index = buffer_pool_->src_acquire_image();

        // Record the command buffer
        record_src_command_buffer(buffer_index);

        // Submit the command buffer to the graphics queue
        const uint64_t ignored     = 0;
        const uint64_t fired_value = timeline_semaphore_value_ + 1;

        timeline_semaphore_value_ += 1;
        VkTimelineSemaphoreSubmitInfo timeline_submit_info{
            VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO, // sType
            nullptr,                                          // pNext
            1,                                                // waitSemaphoreValueCount
            &ignored,                                         // pWaitSemaphoreValues
            1,                                                // signalSemaphoreValueCount
            &fired_value                                      // pSignalSemaphoreValues
        };

        VkSubmitInfo submit_info{
            VK_STRUCTURE_TYPE_SUBMIT_INFO,  // sType
            &timeline_submit_info,          // pNext
            0,                              // waitSemaphoreCount
            nullptr,                        // pWaitSemaphores
            nullptr,                        // pWaitDstStageMask
            1,                              // commandBufferCount
            &app_command_buffer_,           // pCommandBuffers
            1,                              // signalSemaphoreCount
            &app_render_finished_semaphore_ // pSignalSemaphores
        };
        if (timewarp_->is_external()) {
            submit_info.waitSemaphoreCount = 0;
            submit_info.pWaitSemaphores    = nullptr;
            submit_info.pWaitDstStageMask  = nullptr;
        }

        VK_ASSERT_SUCCESS(
            vulkan::locked_queue_submit(display_sink_->queues_[vulkan::queue::queue_type::GRAPHICS], 1, &submit_info, nullptr))

        // Wait for the application to finish rendering
        VkSemaphoreWaitInfo wait_info{
            VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO, // sType
            nullptr,                               // pNext
            0,                                     // flags
            1,                                     // semaphoreCount
            &app_render_finished_semaphore_,       // pSemaphores
            &fired_value                           // pValues
        };
        VK_ASSERT_SUCCESS(vkWaitSemaphores(display_sink_->vk_device_, &wait_info, UINT64_MAX))
        auto pt = fast_pose;
        buffer_pool_->src_release_image(buffer_index, std::move(pt));
    }

    // TODO: for DRM, get vsync estimate
    auto next_swap = vsync_.get_ro_nullable();
    if (next_swap == nullptr) {
        std::this_thread::sleep_for(display_params::period / 6.0 * 5);
        log_->debug("No vsync estimate!");
    } else {
        // convert next_swap_time to std::chrono::time_point
        auto next_swap_time_point = std::chrono::time_point<std::chrono::system_clock>(
            std::chrono::duration_cast<std::chrono::system_clock::duration>((**next_swap).time_since_epoch()));
        auto current_time = clock_->now().time_since_epoch();
        auto diff         = next_swap_time_point - current_time;
        log_->debug("swap diff: {} ms", std::chrono::duration_cast<std::chrono::milliseconds>(diff.time_since_epoch()).count());
        next_swap_time_point -= std::chrono::duration_cast<std::chrono::system_clock::duration>(
            display_params::period / 6.0 * 5); // sleep till 1/6 of the period before vsync to begin timewarp
        std::this_thread::sleep_until(next_swap_time_point);
    }

    if (!timewarp_->is_external()) {
        // Update the timewarp uniforms and submit the timewarp command buffer to the graphics queue
        auto res          = buffer_pool_->post_processing_acquire_image();
        auto buffer_index = res.first;
        auto pose         = res.second;
        timewarp_->update_uniforms(pose, 1);

        if (buffer_index == -1) {
            return;
        }

        record_post_processing_command_buffer(buffer_index, swapchain_image_index_);

        VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        VkSubmitInfo         timewarp_submit_info{
            VK_STRUCTURE_TYPE_SUBMIT_INFO,       // sType
            nullptr,                             // pNext
            1,                                   // waitSemaphoreCount
            &image_available_semaphore_,         // pWaitSemaphores
            wait_stages,                         // pWaitDstStageMask
            1,                                   // commandBufferCount
            &timewarp_command_buffer_,           // pCommandBuffers
            1,                                   // signalSemaphoreCount
            &timewarp_render_finished_semaphore_ // pSignalSemaphores
        };

        VK_ASSERT_SUCCESS(vulkan::locked_queue_submit(display_sink_->queues_[vulkan::queue::queue_type::GRAPHICS], 1,
                                                      &timewarp_submit_info, frame_fence_))

        // Present the rendered image
        VkPresentInfoKHR present_info{
            VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,   // sType
            nullptr,                              // pNext
            1,                                    // waitSemaphoreCount
            &timewarp_render_finished_semaphore_, // pWaitSemaphores
            1,                                    // swapchainCount
            &display_sink_->vk_swapchain_,        // pSwapchains
            &swapchain_image_index_,              // pImageIndices
            nullptr                               // pResults
        };

        VkResult ret = VK_SUCCESS;
        {
            std::lock_guard<std::mutex> lock{*display_sink_->queues_[vulkan::queue::queue_type::GRAPHICS].mutex};
            ret = (vkQueuePresentKHR(display_sink_->queues_[vulkan::queue::queue_type::GRAPHICS].vk_queue, &present_info));
        }

        // Wait for the previous frame to finish rendering
        VK_ASSERT_SUCCESS(vkWaitForFences(display_sink_->vk_device_, 1, &frame_fence_, VK_TRUE, UINT64_MAX))

        swapchain_image_index_ = UINT32_MAX;

        buffer_pool_->post_processing_release_image(buffer_index);

        if (ret == VK_ERROR_OUT_OF_DATE_KHR || ret == VK_SUBOPTIMAL_KHR) {
            display_sink_->recreate_swapchain();
        } else {
            VK_ASSERT_SUCCESS(ret)
        }
    }

    // #ifndef NDEBUG
    // Print the FPS
    if (clock_->now() - last_fps_update_ > std::chrono::milliseconds(1000)) {
        log_->info("FPS: {}", fps_);
        fps_             = 0;
        last_fps_update_ = clock_->now();
    } else {
        fps_++;
    }
    // #endif
}

[[maybe_unused]] void native_renderer::recreate_swapchain() {
    vkDeviceWaitIdle(display_sink_->vk_device_);
    for (auto& framebuffer : swapchain_framebuffers_) {
        vkDestroyFramebuffer(display_sink_->vk_device_, framebuffer, nullptr);
    }
    display_sink_->recreate_swapchain();
    create_swapchain_framebuffers();
}

void native_renderer::create_swapchain_framebuffers() {
    swapchain_framebuffers_.resize(display_sink_->swapchain_image_views_.size());

    for (ulong i = 0; i < display_sink_->swapchain_image_views_.size(); i++) {
        std::array<VkImageView, 1> attachments = {display_sink_->swapchain_image_views_[i]};

        assert(timewarp_pass_ != VK_NULL_HANDLE);
        VkFramebufferCreateInfo framebuffer_info{
            VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, // sType
            nullptr,                                   // pNext
            0,                                         // flags
            timewarp_pass_,                            // renderPass
            attachments.size(),                        // attachmentCount
            attachments.data(),                        // pAttachments
            display_sink_->swapchain_extent_.width,    // width
            display_sink_->swapchain_extent_.height,   // height
            1                                          // layers
        };

        VK_ASSERT_SUCCESS(
            vkCreateFramebuffer(display_sink_->vk_device_, &framebuffer_info, nullptr, &swapchain_framebuffers_[i]))
    }
}

void native_renderer::record_src_command_buffer(vulkan::image_index_t buffer_index) {
    if (!app_->is_external()) {
        // Begin recording app command buffer
        VkCommandBufferBeginInfo begin_info = {
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, // sType
            nullptr,                                     // pNext
            0,                                           // flags
            nullptr                                      // pInheritanceInfo
        };
        VK_ASSERT_SUCCESS(vkBeginCommandBuffer(app_command_buffer_, &begin_info))

        for (auto eye = 0; eye < 2; eye++) {
            assert(app_pass_ != VK_NULL_HANDLE);
            std::array<VkClearValue, 3> clear_values = {};
            clear_values[0].color                    = {{1.0f, 1.0f, 1.0f, 1.0f}};

            // Make sure the depth image is also cleared correctly
            float clear_depth                  = rendering_params::reverse_z ? 0.0f : 1.0f;
            clear_values[1].color              = {{clear_depth, clear_depth, clear_depth, 1.0f}};
            clear_values[2].depthStencil.depth = clear_depth;

            VkRenderPassBeginInfo render_pass_info{
                VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,   // sType
                nullptr,                                    // pNext
                app_pass_,                                  // renderPass
                offscreen_framebuffers_[buffer_index][eye], // framebuffer
                {
                    {0, 0},                                                                               // offset
                    {display_sink_->swapchain_extent_.width / 2, display_sink_->swapchain_extent_.height} // extent
                },                                                                                        // renderArea
                clear_values.size(),                                                                      // clearValueCount
                clear_values.data()                                                                       // pClearValues
            };

            vkCmdBeginRenderPass(app_command_buffer_, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
            // Call app service to record the command buffer
            app_->record_command_buffer(app_command_buffer_, offscreen_framebuffers_[buffer_index][eye], buffer_index,
                                        eye == 0, nullptr);
            vkCmdEndRenderPass(app_command_buffer_);
        }
        VK_ASSERT_SUCCESS(vkEndCommandBuffer(app_command_buffer_))
    }
}

void native_renderer::record_post_processing_command_buffer(vulkan::image_index_t buffer_index,
                                                            uint32_t              swapchain_image_index) {
    // Begin recording timewarp command buffer
    VkCommandBufferBeginInfo timewarp_begin_info = {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, // sType
        nullptr,                                     // pNext
        0,                                           // flags
        nullptr                                      // pInheritanceInfo
    };
    VK_ASSERT_SUCCESS(vkBeginCommandBuffer(timewarp_command_buffer_, &timewarp_begin_info))

    for (auto eye = 0; eye < 2; eye++) {
        timewarp_->record_command_buffer(timewarp_command_buffer_, swapchain_framebuffers_[swapchain_image_index], buffer_index,
                                         eye == 0, nullptr);
    }

    VK_ASSERT_SUCCESS(vkEndCommandBuffer(timewarp_command_buffer_))
}

void native_renderer::create_sync_objects() {
    VkSemaphoreTypeCreateInfo timeline_semaphore_info{
        VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO, // sType
        nullptr,                                      // pNext
        VK_SEMAPHORE_TYPE_TIMELINE,                   // semaphoreType
        0                                             // initialValue
    };

    VkSemaphoreCreateInfo create_info{
        VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, // sType
        &timeline_semaphore_info,                // pNext
        0                                        // flags
    };

    VK_ASSERT_SUCCESS(vkCreateSemaphore(display_sink_->vk_device_, &create_info, nullptr, &app_render_finished_semaphore_))
    deletion_queue_.emplace([=]() {
        vkDestroySemaphore(display_sink_->vk_device_, app_render_finished_semaphore_, nullptr);
    });

    VkSemaphoreCreateInfo semaphore_info{
        VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, // sType
        nullptr,                                 // pNext
        0                                        // flags
    };
    VkFenceCreateInfo fence_info{
        VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, // sType
        nullptr,                             // pNext
        VK_FENCE_CREATE_SIGNALED_BIT         // flags
    };

    VK_ASSERT_SUCCESS(vkCreateSemaphore(display_sink_->vk_device_, &semaphore_info, nullptr, &image_available_semaphore_))
    deletion_queue_.emplace([=]() {
        vkDestroySemaphore(display_sink_->vk_device_, image_available_semaphore_, nullptr);
    });

    VK_ASSERT_SUCCESS(
        vkCreateSemaphore(display_sink_->vk_device_, &semaphore_info, nullptr, &timewarp_render_finished_semaphore_))
    deletion_queue_.emplace([=]() {
        vkDestroySemaphore(display_sink_->vk_device_, timewarp_render_finished_semaphore_, nullptr);
    });

    VK_ASSERT_SUCCESS(vkCreateFence(display_sink_->vk_device_, &fence_info, nullptr, &frame_fence_))
    deletion_queue_.emplace([=]() {
        vkDestroyFence(display_sink_->vk_device_, frame_fence_, nullptr);
    });
}

void native_renderer::create_depth_image(vulkan::vk_image& depth_image) {
    VkImageCreateInfo image_info{
        VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, // sType
        nullptr,                             // pNext
        0,                                   // flags
        VK_IMAGE_TYPE_2D,                    // imageType
        VK_FORMAT_D32_SFLOAT,                // format
        {
            width_ / 2,                                                           // width
            height_,                                                              // height
            1                                                                     // depth
        },                                                                        // extent
        1,                                                                        // mipLevels
        1,                                                                        // arrayLayers
        VK_SAMPLE_COUNT_1_BIT,                                                    // samples
        VK_IMAGE_TILING_OPTIMAL,                                                  // tiling
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, // usage
        {},                                                                       // sharingMode
        0,                                                                        // queueFamilyIndexCount
        nullptr,                                                                  // pQueueFamilyIndices
        {}                                                                        // initialLayout
    };

    depth_image.image_info = image_info;

    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    VK_ASSERT_SUCCESS(vmaCreateImage(display_sink_->vma_allocator_, &image_info, &alloc_info, &depth_image.image,
                                     &depth_image.allocation, &depth_image.allocation_info))
    deletion_queue_.emplace([=]() {
        vmaDestroyImage(display_sink_->vma_allocator_, depth_image.image, depth_image.allocation);
    });

    VkImageViewCreateInfo view_info{
        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, // sType
        nullptr,                                  // pNext
        0,                                        // flags
        depth_image.image,                        // image
        VK_IMAGE_VIEW_TYPE_2D,                    // viewType
        VK_FORMAT_D32_SFLOAT,                     // format
        {},                                       // components
        {
            VK_IMAGE_ASPECT_DEPTH_BIT, // aspectMask
            0,                         // baseMipLevel
            1,                         // levelCount
            0,                         // baseArrayLayer
            1                          // layerCount
        } // subresourceRange
    };

    VK_ASSERT_SUCCESS(vkCreateImageView(display_sink_->vk_device_, &view_info, nullptr, &depth_image.image_view))
    deletion_queue_.emplace([=]() {
        vkDestroyImageView(display_sink_->vk_device_, depth_image.image_view, nullptr);
    });
}

void native_renderer::create_offscreen_pool() {
    VkImageCreateInfo sample_create_info{
        VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, // sType
        nullptr,                             // pNext
        0,                                   // flags
        VK_IMAGE_TYPE_2D,                    // imageType
        VK_FORMAT_B8G8R8A8_UNORM,            // format
        {
            width_ / 2,                                                 // width
            height_,                                                    // height
            1                                                           // depth
        },                                                              // extent
        1,                                                              // mipLevels
        1,                                                              // arrayLayers
        VK_SAMPLE_COUNT_1_BIT,                                          // samples
        export_dma_ ? VK_IMAGE_TILING_LINEAR : VK_IMAGE_TILING_OPTIMAL, // tiling
        static_cast<VkImageUsageFlags>(
            (VK_IMAGE_USAGE_TRANSFER_DST_BIT | (export_dma_ ? 0 : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) |
            (timewarp_->is_external() ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : VK_IMAGE_USAGE_SAMPLED_BIT)), // usage
        {},                                                                                             // sharingMode
        0,                                                                                              // queueFamilyIndexCount
        nullptr,                                                                                        // pQueueFamilyIndices
        {}                                                                                              // initialLayout
    };

    uint32_t                mem_type_index;
    VmaAllocationCreateInfo alloc_info{.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT, .usage = VMA_MEMORY_USAGE_GPU_ONLY};
    vmaFindMemoryTypeIndexForImageInfo(display_sink_->vma_allocator_, &sample_create_info, &alloc_info, &mem_type_index);

    offscreen_export_mem_alloc_info_.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
    offscreen_export_mem_alloc_info_.handleTypes =
        export_dma_ ? VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT : VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

    VmaPoolCreateInfo pool_create_info   = {};
    pool_create_info.memoryTypeIndex     = mem_type_index;
    pool_create_info.blockSize           = 0;
    pool_create_info.maxBlockCount       = 0;
    pool_create_info.pMemoryAllocateNext = &offscreen_export_mem_alloc_info_;
    this->offscreen_pool_create_info_    = pool_create_info;

    VK_ASSERT_SUCCESS(vmaCreatePool(display_sink_->vma_allocator_, &offscreen_pool_create_info_, &offscreen_pool_));
    deletion_queue_.emplace([=]() {
        vmaDestroyPool(display_sink_->vma_allocator_, offscreen_pool_);
    });
}

void native_renderer::create_offscreen_target(vulkan::vk_image& image) {
    if (timewarp_->is_external() || app_->is_external()) {
        assert(offscreen_pool_ != VK_NULL_HANDLE);
        image.export_image_info = {VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO, nullptr,
                                   export_dma_ ? VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT
                                               : VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT};
    }

    std::vector<uint32_t> queue_family_indices;
    queue_family_indices.push_back(display_sink_->queues_[vulkan::queue::queue_type::GRAPHICS].family);
    if (display_sink_->queues_.find(vulkan::queue::queue_type::COMPUTE) != display_sink_->queues_.end() &&
        display_sink_->queues_.find(vulkan::queue::queue_type::COMPUTE)->second.family !=
            display_sink_->queues_[vulkan::queue::queue_type::GRAPHICS].family) {
        queue_family_indices.push_back(display_sink_->queues_[vulkan::queue::queue_type::COMPUTE].family);
    }
    if (display_sink_->queues_.find(vulkan::queue::queue_type::DEDICATED_TRANSFER) != display_sink_->queues_.end()) {
        queue_family_indices.push_back(display_sink_->queues_[vulkan::queue::queue_type::DEDICATED_TRANSFER].family);
    }

    image.image_info = {
        VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,                                                    // sType
        (app_->is_external() || timewarp_->is_external()) ? &image.export_image_info : nullptr, // pNext
        0,                                                                                      // flags
        VK_IMAGE_TYPE_2D,                                                                       // imageType
        VK_FORMAT_B8G8R8A8_UNORM,                                                               // format
        {
            width_ / 2,          // width
            height_,             // height
            1                    // depth
        },                       // extent
        1,                       // mipLevels
        1,                       // arrayLayers
        VK_SAMPLE_COUNT_1_BIT,   // samples
        VK_IMAGE_TILING_OPTIMAL, // tiling
        static_cast<VkImageUsageFlags>(
            (VK_IMAGE_USAGE_TRANSFER_DST_BIT | (export_dma_ ? 0 : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) |
            (timewarp_->is_external() ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : VK_IMAGE_USAGE_SAMPLED_BIT)), // usage
        VK_SHARING_MODE_CONCURRENT,                                                                     // sharingMode
        static_cast<uint32_t>(queue_family_indices.size()),                                             // queueFamilyIndexCount
        queue_family_indices.data(),                                                                    // pQueueFamilyIndices
        VK_IMAGE_LAYOUT_UNDEFINED                                                                       // initialLayout
    };

    VmaAllocationCreateInfo alloc_info{.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT, .usage = VMA_MEMORY_USAGE_GPU_ONLY};
    if (timewarp_->is_external() || app_->is_external()) {
        alloc_info.pool = offscreen_pool_;
    }

    VK_ASSERT_SUCCESS(vmaCreateImage(display_sink_->vma_allocator_, &image.image_info, &alloc_info, &image.image,
                                     &image.allocation, &image.allocation_info))
    assert(image.allocation_info.deviceMemory);
    deletion_queue_.emplace([=]() {
        vmaDestroyImage(display_sink_->vma_allocator_, image.image, image.allocation);
    });

    VkImageViewCreateInfo view_info{
        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, // sType
        nullptr,                                  // pNext
        0,                                        // flags
        image.image,                              // image
        VK_IMAGE_VIEW_TYPE_2D,                    // viewType
        VK_FORMAT_B8G8R8A8_UNORM,                 // format
        {},                                       // components
        {
            VK_IMAGE_ASPECT_COLOR_BIT, // aspectMask
            0,                         // baseMipLevel
            1,                         // levelCount
            0,                         // baseArrayLayer
            1                          // layerCount
        } // subresourceRange
    };

    VK_ASSERT_SUCCESS(vkCreateImageView(display_sink_->vk_device_, &view_info, nullptr, &image.image_view))

    if (export_dma_) {
        VkMemoryGetFdInfoKHR get_fd_info{VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR, // sType
                                         nullptr,                                  // pNext
                                         image.allocation_info.deviceMemory,       // memory
                                         VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT};

        auto GetMemoryFdKHR =
            reinterpret_cast<PFN_vkGetMemoryFdKHR>(vkGetDeviceProcAddr(display_sink_->vk_device_, "vkGetMemoryFdKHR"));

        VK_ASSERT_SUCCESS(GetMemoryFdKHR(display_sink_->vk_device_, &get_fd_info, &image.fd))
    }

    deletion_queue_.emplace([=]() {
        vkDestroyImageView(display_sink_->vk_device_, image.image_view, nullptr);
    });
}

void native_renderer::create_offscreen_framebuffers() {
    offscreen_framebuffers_.resize(NATIVE_RENDERER_BUFFER_POOL_SIZE);

    for (auto i = 0; i < NATIVE_RENDERER_BUFFER_POOL_SIZE; i++) {
        for (auto eye = 0; eye < 2; eye++) {
            std::array<VkImageView, 3> attachments = {offscreen_images_[i][eye].image_view, depth_images_[i][eye].image_view,
                                                      depth_attachment_images_[i][eye].image_view};

            VkFramebufferCreateInfo framebuffer_info{
                VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,  // sType
                nullptr,                                    // pNext
                0,                                          // flags
                app_pass_,                                  // renderPass
                static_cast<uint32_t>(attachments.size()),  // attachmentCount
                attachments.data(),                         // pAttachments
                display_sink_->swapchain_extent_.width / 2, // width
                display_sink_->swapchain_extent_.height,    // height
                1                                           // layers
            };

            VK_ASSERT_SUCCESS(
                vkCreateFramebuffer(display_sink_->vk_device_, &framebuffer_info, nullptr, &offscreen_framebuffers_[i][eye]))

            deletion_queue_.emplace([=]() {
                vkDestroyFramebuffer(display_sink_->vk_device_, offscreen_framebuffers_[i][eye], nullptr);
            });
        }
    }
}

void native_renderer::create_app_pass() {
    std::array<VkAttachmentDescription, 3> attachment_descriptions{
        {{
             0,                                // flags
             VK_FORMAT_B8G8R8A8_UNORM,         // format
             VK_SAMPLE_COUNT_1_BIT,            // samples
             VK_ATTACHMENT_LOAD_OP_CLEAR,      // loadOp
             VK_ATTACHMENT_STORE_OP_STORE,     // storeOp
             VK_ATTACHMENT_LOAD_OP_DONT_CARE,  // stencilLoadOp
             VK_ATTACHMENT_STORE_OP_DONT_CARE, // stencilStoreOp
             VK_IMAGE_LAYOUT_UNDEFINED,        // initialLayout
             timewarp_->is_external() ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
                                      : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL // finalLayout
         },
         {
             0,                                // flags
             VK_FORMAT_B8G8R8A8_UNORM,         // format
             VK_SAMPLE_COUNT_1_BIT,            // samples
             VK_ATTACHMENT_LOAD_OP_CLEAR,      // loadOp
             VK_ATTACHMENT_STORE_OP_STORE,     // storeOp
             VK_ATTACHMENT_LOAD_OP_DONT_CARE,  // stencilLoadOp
             VK_ATTACHMENT_STORE_OP_DONT_CARE, // stencilStoreOp
             VK_IMAGE_LAYOUT_UNDEFINED,        // initialLayout
             timewarp_->is_external() ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
                                      : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL // finalLayout
         },
         {
             0,                                               // flags
             VK_FORMAT_D32_SFLOAT,                            // format
             VK_SAMPLE_COUNT_1_BIT,                           // samples
             VK_ATTACHMENT_LOAD_OP_CLEAR,                     // loadOp
             VK_ATTACHMENT_STORE_OP_DONT_CARE,                // storeOp
             VK_ATTACHMENT_LOAD_OP_DONT_CARE,                 // stencilLoadOp
             VK_ATTACHMENT_STORE_OP_DONT_CARE,                // stencilStoreOp
             VK_IMAGE_LAYOUT_UNDEFINED,                       // initialLayout
             VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL // finalLayout
         }}};

    VkAttachmentReference color_attachment_ref{
        0,                                       // attachment
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL // layout
    };

    VkAttachmentReference depth_image_attachment_ref{
        1,                                       // attachment
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL // layout
    };

    VkAttachmentReference color_refs[2] = {color_attachment_ref, depth_image_attachment_ref};

    VkAttachmentReference depth_attachment_ref{
        2,                                               // attachment
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL // layout
    };

    VkSubpassDescription subpass = {
        0,                               // flags
        VK_PIPELINE_BIND_POINT_GRAPHICS, // pipelineBindPoint
        0,                               // inputAttachmentCount
        nullptr,                         // pInputAttachments
        2,                               // colorAttachmentCount
        color_refs,                      // pColorAttachments
        nullptr,                         // pResolveAttachments
        &depth_attachment_ref,           // pDepthStencilAttachment
        0,                               // preserveAttachmentCount
        nullptr                          // pPreserveAttachments
    };

    // Subpass dependencies for layout transitions
    std::array<VkSubpassDependency, 3> dependencies{
        {{
             // After timewarp samples from the offscreen image, it needs to be transitioned to a color attachment
             VK_SUBPASS_EXTERNAL,                                                                               // srcSubpass
             0,                                                                                                 // dstSubpass
             timewarp_->is_external() ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, // srcStageMask
             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,                                                     // dstStageMask
             timewarp_->is_external() ? VK_ACCESS_TRANSFER_READ_BIT : VK_ACCESS_SHADER_READ_BIT,                // srcAccessMask
             VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,                                                              // dstAccessMask
             VK_DEPENDENCY_BY_REGION_BIT // dependencyFlags
         },
         {
             // After the app is done rendering to the offscreen image, it needs to be transitioned to a shader read
             0,                                             // srcSubpass
             VK_SUBPASS_EXTERNAL,                           // dstSubpass
             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, // srcStageMask
             timewarp_->is_external()
                 ? VK_PIPELINE_STAGE_TRANSFER_BIT
                 : VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,  // dstStageMask
             VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,                                               // srcAccessMask
             timewarp_->is_external() ? VK_ACCESS_TRANSFER_READ_BIT : VK_ACCESS_SHADER_READ_BIT, // dstAccessMask
             VK_DEPENDENCY_BY_REGION_BIT                                                         // dependencyFlags
         },
         {// depth buffer write-after-write hazard
          VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT, 0}}};

    VkRenderPassCreateInfo render_pass_info{
        VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,             // sType
        nullptr,                                               // pNext
        0,                                                     // flags
        static_cast<uint32_t>(attachment_descriptions.size()), // attachmentCount
        attachment_descriptions.data(),                        // pAttachments
        1,                                                     // subpassCount
        &subpass,                                              // pSubpasses
        static_cast<uint32_t>(dependencies.size()),            // dependencyCount
        dependencies.data()                                    // pDependencies
    };
    VK_ASSERT_SUCCESS(vkCreateRenderPass(display_sink_->vk_device_, &render_pass_info, nullptr, &app_pass_))
    deletion_queue_.emplace([=]() {
        vkDestroyRenderPass(display_sink_->vk_device_, app_pass_, nullptr);
    });
}

/**
 * @brief Creates a render pass for timewarp.
 */
void native_renderer::create_timewarp_pass() {
    std::array<VkAttachmentDescription, 1> attchmentDescriptions{{{
        0,                                             // flags
        display_sink_->swapchain_image_format_.format, // format
        VK_SAMPLE_COUNT_1_BIT,                         // samples
        VK_ATTACHMENT_LOAD_OP_DONT_CARE,               // loadOp
        VK_ATTACHMENT_STORE_OP_STORE,                  // storeOp
        VK_ATTACHMENT_LOAD_OP_DONT_CARE,               // stencilLoadOp
        VK_ATTACHMENT_STORE_OP_DONT_CARE,              // stencilStoreOp
        VK_IMAGE_LAYOUT_UNDEFINED,                     // initialLayout
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR                // finalLayout
    }}};

    VkAttachmentReference color_attachment_ref{
        0,                                       // attachment
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL // layout
    };

    VkSubpassDependency dependency = {
        .srcSubpass    = VK_SUBPASS_EXTERNAL,
        .dstSubpass    = 0,
        .srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    };

    VkSubpassDescription subpass = {
        0,                               // flags
        VK_PIPELINE_BIND_POINT_GRAPHICS, // pipelineBindPoint
        0,                               // inputAttachmentCount
        nullptr,                         // pInputAttachments
        1,                               // colorAttachmentCount
        &color_attachment_ref,           // pColorAttachments
        nullptr,                         // pResolveAttachments
        nullptr,                         // pDepthStencilAttachment
        0,                               // preserveAttachmentCount
        nullptr                          // pPreserveAttachments
    };

    // assert(app_->is_external());

    VkRenderPassCreateInfo render_pass_info{
        VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,           // sType
        nullptr,                                             // pNext
        0,                                                   // flags
        static_cast<uint32_t>(attchmentDescriptions.size()), // attachmentCount
        attchmentDescriptions.data(),                        // pAttachments
        1,                                                   // subpassCount
        &subpass,                                            // pSubpasses
        1,                                                   // dependencyCount
        &dependency                                          // pDependencies
    };

    VK_ASSERT_SUCCESS(vkCreateRenderPass(display_sink_->vk_device_, &render_pass_info, nullptr, &timewarp_pass_))
    deletion_queue_.emplace([=]() {
        vkDestroyRenderPass(display_sink_->vk_device_, timewarp_pass_, nullptr);
    });
}

PLUGIN_MAIN(native_renderer)
