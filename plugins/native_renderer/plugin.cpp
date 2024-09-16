#define VMA_IMPLEMENTATION

#include "plugin.hpp"
#include "illixr/global_module_defs.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include <vulkan/vulkan_core.h>

#define TINYOBJLOADER_IMPLEMENTATION
#include "illixr/gl_util/lib/tiny_obj_loader.h"

using namespace ILLIXR;

[[maybe_unused]] native_renderer::native_renderer(const std::string& name, phonebook* pb)
    : threadloop{name, pb}
    , switchboard_{phonebook_->lookup_impl<switchboard>()}
    , pose_prediction_{phonebook_->lookup_impl<pose_prediction>()}
    , display_sink_{phonebook_->lookup_impl<display_sink>()}
    , timewarp_{phonebook_->lookup_impl<timewarp>()}
    , app_{phonebook_->lookup_impl<app>()}
    , clock_{phonebook_->lookup_impl<relative_clock>()}
    , last_fps_update_{std::chrono::duration<long, std::nano>{0}} {
    spdlogger(std::getenv("NATIVE_RENDERER_LOG_LEVEL"));
}

void native_renderer::_p_thread_setup() {
    for (auto i = 0; i < 2; i++) {
        create_depth_image(&depth_images_[i], &depth_image_allocations_[i], &depth_image_views_[i]);
    }
    for (auto i = 0; i < 2; i++) {
        create_offscreen_target(&offscreen_images_[i], &offscreen_image_allocations_[i], &offscreen_image_views_[i],
                                &offscreen_framebuffers_[i]);
    }
    command_pool_       = vulkan_utils::create_command_pool(display_sink_->vk_device, display_sink_->graphics_queue_family);
    app_command_buffer_ = vulkan_utils::create_command_buffer(display_sink_->vk_device, command_pool_);
    timewarp_command_buffer_ = vulkan_utils::create_command_buffer(display_sink_->vk_device, command_pool_);
    create_sync_objects();
    create_app_pass();
    create_timewarp_pass();
    create_sync_objects();
    create_offscreen_framebuffers();
    create_swapchain_framebuffers();
    app_->setup(app_pass_, 0);
    timewarp_->setup(timewarp_pass_, 0, {std::vector{offscreen_image_views_[0]}, std::vector{offscreen_image_views_[1]}},
                     true);
}

void native_renderer::_p_one_iteration() {
    display_sink_->poll_window_events();

    // Wait for the previous frame to finish rendering
    VK_ASSERT_SUCCESS(vkWaitForFences(display_sink_->vk_device, 1, &frame_fence_, VK_TRUE, UINT64_MAX))

    // Acquire the next image from the swapchain
    uint32_t swapchain_image_index;
    auto     ret = (vkAcquireNextImageKHR(display_sink_->vk_device, display_sink_->vk_swapchain, UINT64_MAX,
                                          image_available_semaphore_, VK_NULL_HANDLE, &swapchain_image_index));

    // Check if the swapchain is out of date or suboptimal
    if (ret == VK_ERROR_OUT_OF_DATE_KHR || ret == VK_SUBOPTIMAL_KHR) {
        display_sink_->recreate_swapchain();
        return;
    } else {
        VK_ASSERT_SUCCESS(ret)
    }
    VK_ASSERT_SUCCESS(vkResetFences(display_sink_->vk_device, 1, &frame_fence_))

    // Get the current fast pose and update the uniforms
    auto fast_pose = pose_prediction_->get_fast_pose();
    app_->update_uniforms(fast_pose.pose);

    // Record the command buffer
    VK_ASSERT_SUCCESS(vkResetCommandBuffer(app_command_buffer_, 0))
    record_command_buffer(swapchain_image_index);

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

    VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSubmitInfo         submit_info{
        VK_STRUCTURE_TYPE_SUBMIT_INFO,  // sType
        &timeline_submit_info,          // pNext
        1,                              // waitSemaphoreCount
        &image_available_semaphore_,    // pWaitSemaphores
        wait_stages,                    // pWaitDstStageMask
        1,                              // commandBufferCount
        &app_command_buffer_,           // pCommandBuffers
        1,                              // signalSemaphoreCount
        &app_render_finished_semaphore_ // pSignalSemaphores
    };

    VK_ASSERT_SUCCESS(vkQueueSubmit(display_sink_->graphics_queue, 1, &submit_info, nullptr))

    // Wait for the application to finish rendering
    VkSemaphoreWaitInfo wait_info{
        VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO, // sType
        nullptr,                               // pNext
        0,                                     // flags
        1,                                     // semaphoreCount
        &app_render_finished_semaphore_,       // pSemaphores
        &fired_value                           // pValues
    };
    VK_ASSERT_SUCCESS(vkWaitSemaphores(display_sink_->vk_device, &wait_info, UINT64_MAX))

    // TODO: for DRM, get vsync estimate
    std::this_thread::sleep_for(display_params::period / 6.0 * 5);

    // Update the timewarp uniforms and submit the timewarp command buffer to the graphics queue
    timewarp_->update_uniforms(fast_pose.pose);
    VkSubmitInfo timewarp_submit_info{
        VK_STRUCTURE_TYPE_SUBMIT_INFO,       // sType
        nullptr,                             // pNext
        0,                                   // waitSemaphoreCount
        nullptr,                             // pWaitSemaphores
        nullptr,                             // pWaitDstStageMask
        1,                                   // commandBufferCount
        &timewarp_command_buffer_,           // pCommandBuffers
        1,                                   // signalSemaphoreCount
        &timewarp_render_finished_semaphore_ // pSignalSemaphores
    };

    VK_ASSERT_SUCCESS(vkQueueSubmit(display_sink_->graphics_queue, 1, &timewarp_submit_info, frame_fence_))

    // Present the rendered image
    VkPresentInfoKHR present_info{
        VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,   // sType
        nullptr,                              // pNext
        1,                                    // waitSemaphoreCount
        &timewarp_render_finished_semaphore_, // pWaitSemaphores
        1,                                    // swapchainCount
        &display_sink_->vk_swapchain,         // pSwapchains
        &swapchain_image_index,               // pImageIndices
        nullptr                               // pResults
    };

    ret = (vkQueuePresentKHR(display_sink_->graphics_queue, &present_info));
    if (ret == VK_ERROR_OUT_OF_DATE_KHR || ret == VK_SUBOPTIMAL_KHR) {
        display_sink_->recreate_swapchain();
    } else {
        VK_ASSERT_SUCCESS(ret)
    }

    // #ifndef NDEBUG
    // Print the FPS
    if (clock_->now() - last_fps_update_ > std::chrono::milliseconds(1000)) {
        // std::cout << "FPS: " << fps_ << std::endl;
        fps_             = 0;
        last_fps_update_ = clock_->now();
    } else {
        fps_++;
    }
    // #endif
}

[[maybe_unused]] void native_renderer::recreate_swapchain() {
    vkDeviceWaitIdle(display_sink_->vk_device);
    for (auto& framebuffer : swapchain_framebuffers_) {
        vkDestroyFramebuffer(display_sink_->vk_device, framebuffer, nullptr);
    }
    display_sink_->recreate_swapchain();
    create_swapchain_framebuffers();
}

void native_renderer::create_swapchain_framebuffers() {
    swapchain_framebuffers_.resize(display_sink_->swapchain_image_views.size());

    for (ulong i = 0; i < display_sink_->swapchain_image_views.size(); i++) {
        std::array<VkImageView, 1> attachments = {display_sink_->swapchain_image_views[i]};

        assert(timewarp_pass_ != VK_NULL_HANDLE);
        VkFramebufferCreateInfo framebuffer_info{
            VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, // sType
            nullptr,                                   // pNext
            0,                                         // flags
            timewarp_pass_,                            // renderPass
            attachments.size(),                        // attachmentCount
            attachments.data(),                        // pAttachments
            display_sink_->swapchain_extent.width,     // width
            display_sink_->swapchain_extent.height,    // height
            1                                          // layers
        };

        VK_ASSERT_SUCCESS(
            vkCreateFramebuffer(display_sink_->vk_device, &framebuffer_info, nullptr, &swapchain_framebuffers_[i]))
    }
}

void native_renderer::record_command_buffer(uint32_t swapchain_image_index) {
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
        std::array<VkClearValue, 2> clear_values = {};
        clear_values[0].color                    = {{1.0f, 1.0f, 1.0f, 1.0f}};
        clear_values[1].depthStencil             = {1.0f, 0};

        VkRenderPassBeginInfo render_pass_info{
            VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, // sType
            nullptr,                                  // pNext
            app_pass_,                                // renderPass
            offscreen_framebuffers_[eye],             // framebuffer
            {
                {0, 0},                         // offset
                display_sink_->swapchain_extent // extent
            },                                  // renderArea
            clear_values.size(),                // clearValueCount
            clear_values.data()                 // pClearValues
        };

        vkCmdBeginRenderPass(app_command_buffer_, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
        // Call app service to record the command buffer
        app_->record_command_buffer(app_command_buffer_, eye);
        vkCmdEndRenderPass(app_command_buffer_);
    }
    VK_ASSERT_SUCCESS(vkEndCommandBuffer(app_command_buffer_))

    // Begin recording timewarp command buffer
    VkCommandBufferBeginInfo timewarp_begin_info = {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, // sType
        nullptr,                                     // pNext
        0,                                           // flags
        nullptr                                      // pInheritanceInfo
    };
    VK_ASSERT_SUCCESS(vkBeginCommandBuffer(timewarp_command_buffer_, &timewarp_begin_info)) {
        assert(timewarp_pass_ != VK_NULL_HANDLE);
        VkClearValue          clear_value{.color = {{0.0f, 0.0f, 0.0f, 1.0f}}};
        VkRenderPassBeginInfo render_pass_info{
            VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,       // sType
            nullptr,                                        // pNext
            timewarp_pass_,                                 // renderPass
            swapchain_framebuffers_[swapchain_image_index], // framebuffer
            {
                {0, 0},                         // offset
                display_sink_->swapchain_extent // extent
            },                                  // renderArea
            1,                                  // clearValueCount
            &clear_value                        // pClearValues
        };

        vkCmdBeginRenderPass(timewarp_command_buffer_, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

        for (auto eye = 0; eye < 2; eye++) {
            VkViewport viewport{
                static_cast<float>(display_sink_->swapchain_extent.width / 2. * eye), // x
                0.0f,                                                                 // y
                static_cast<float>(display_sink_->swapchain_extent.width),            // width
                static_cast<float>(display_sink_->swapchain_extent.height),           // height
                0.0f,                                                                 // minDepth
                1.0f                                                                  // maxDepth
            };
            vkCmdSetViewport(timewarp_command_buffer_, 0, 1, &viewport);

            VkRect2D scissor{
                {0, 0},                         // offset
                display_sink_->swapchain_extent // extent
            };
            vkCmdSetScissor(timewarp_command_buffer_, 0, 1, &scissor);

            // Call timewarp service to record the command buffer
            timewarp_->record_command_buffer(timewarp_command_buffer_, 0, eye == 0);
        }
        vkCmdEndRenderPass(timewarp_command_buffer_);
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

    vkCreateSemaphore(display_sink_->vk_device, &create_info, nullptr, &app_render_finished_semaphore_);

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

    VK_ASSERT_SUCCESS(vkCreateSemaphore(display_sink_->vk_device, &semaphore_info, nullptr, &image_available_semaphore_))
    VK_ASSERT_SUCCESS(
        vkCreateSemaphore(display_sink_->vk_device, &semaphore_info, nullptr, &timewarp_render_finished_semaphore_))
    VK_ASSERT_SUCCESS(vkCreateFence(display_sink_->vk_device, &fence_info, nullptr, &frame_fence_))
}

void native_renderer::create_depth_image(VkImage* depth_image,
                                         VmaAllocation* depth_image_allocation,
                                         VkImageView* depth_image_view) {
    VkImageCreateInfo image_info{
        VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, // sType
        nullptr,                             // pNext
        0,                                   // flags
        VK_IMAGE_TYPE_2D,                    // imageType
        VK_FORMAT_D32_SFLOAT,                // format
        {
            display_params::width_pixels,                                         // width
            display_params::height_pixels,                                        // height
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

    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    VK_ASSERT_SUCCESS(vmaCreateImage(display_sink_->vma_allocator, &image_info, &alloc_info, depth_image,
                                     depth_image_allocation, nullptr))

    VkImageViewCreateInfo view_info{
        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, // sType
        nullptr,                                  // pNext
        0,                                        // flags
        *depth_image,                             // image
        VK_IMAGE_VIEW_TYPE_2D,                    // viewType
        VK_FORMAT_D32_SFLOAT,                     // format
        {},                                       // components
        {
            VK_IMAGE_ASPECT_DEPTH_BIT, // aspectMask
            0,                         // baseMipLevel
            1,                         // levelCount
            0,                         // baseArrayLayer
            1                          // layerCount
        }                              // subresourceRange
    };

    VK_ASSERT_SUCCESS(vkCreateImageView(display_sink_->vk_device, &view_info, nullptr, depth_image_view))
}

void native_renderer::create_offscreen_target(VkImage* offscreen_image,
                                              VmaAllocation* offscreen_image_allocation,
                                              VkImageView* offscreen_image_view,
                                              [[maybe_unused]] VkFramebuffer* offscreen_framebuffer) {
    VkImageCreateInfo image_info{
        VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, // sType
        nullptr,                             // pNext
        0,                                   // flags
        VK_IMAGE_TYPE_2D,                    // imageType
        VK_FORMAT_B8G8R8A8_UNORM,            // format
        {
            display_params::width_pixels,                                 // width
            display_params::height_pixels,                                // height
            1                                                             // depth
        },                                                                // extent
        1,                                                                // mipLevels
        1,                                                                // arrayLayers
        VK_SAMPLE_COUNT_1_BIT,                                            // samples
        VK_IMAGE_TILING_OPTIMAL,                                          // tiling
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, // usage
        {},                                                               // sharingMode
        0,                                                                // queueFamilyIndexCount
        nullptr,                                                          // pQueueFamilyIndices
        {}                                                                // initialLayout
    };

    VmaAllocationCreateInfo alloc_info{.usage = VMA_MEMORY_USAGE_GPU_ONLY};

    VK_ASSERT_SUCCESS(vmaCreateImage(display_sink_->vma_allocator, &image_info, &alloc_info, offscreen_image,
                                     offscreen_image_allocation, nullptr))

    VkImageViewCreateInfo view_info{
        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, // sType
        nullptr,                                  // pNext
        0,                                        // flags
        *offscreen_image,                         // image
        VK_IMAGE_VIEW_TYPE_2D,                    // viewType
        VK_FORMAT_B8G8R8A8_UNORM,                 // format
        {},                                       // components
        {
            VK_IMAGE_ASPECT_COLOR_BIT, // aspectMask
            0,                         // baseMipLevel
            1,                         // levelCount
            0,                         // baseArrayLayer
            1                          // layerCount
        }                              // subresourceRange
    };

    VK_ASSERT_SUCCESS(vkCreateImageView(display_sink_->vk_device, &view_info, nullptr, offscreen_image_view))
}

void native_renderer::create_offscreen_framebuffers() {
    for (auto eye = 0; eye < 2; eye++) {
        std::array<VkImageView, 2> attachments = {offscreen_image_views_[eye], depth_image_views_[eye]};

        assert(app_pass_ != VK_NULL_HANDLE);
        VkFramebufferCreateInfo framebuffer_info{
            VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, // sType
            nullptr,                                   // pNext
            0,                                         // flags
            app_pass_,                                 // renderPass
            static_cast<uint32_t>(attachments.size()), // attachmentCount
            attachments.data(),                        // pAttachments
            display_params::width_pixels,              // width
            display_params::height_pixels,             // height
            1                                          // layers
        };

        VK_ASSERT_SUCCESS(
            vkCreateFramebuffer(display_sink_->vk_device, &framebuffer_info, nullptr, &offscreen_framebuffers_[eye]))
    }
}

void native_renderer::create_app_pass() {
    std::array<VkAttachmentDescription, 2> attachment_descriptions{
        {{
             0,                                       // flags
             VK_FORMAT_B8G8R8A8_UNORM,                // format
             VK_SAMPLE_COUNT_1_BIT,                   // samples
             VK_ATTACHMENT_LOAD_OP_CLEAR,             // loadOp
             VK_ATTACHMENT_STORE_OP_STORE,            // storeOp
             VK_ATTACHMENT_LOAD_OP_DONT_CARE,         // stencilLoadOp
             VK_ATTACHMENT_STORE_OP_DONT_CARE,        // stencilStoreOp
             VK_IMAGE_LAYOUT_UNDEFINED,               // initialLayout
             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL // finalLayout
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

    VkAttachmentReference depth_attachment_ref{
        1,                                               // attachment
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL // layout
    };

    VkSubpassDescription subpass = {
        0,                               // flags
        VK_PIPELINE_BIND_POINT_GRAPHICS, // pipelineBindPoint
        0,                               // inputAttachmentCount
        nullptr,                         // pInputAttachments
        1,                               // colorAttachmentCount
        &color_attachment_ref,           // pColorAttachments
        nullptr,                         // pResolveAttachments
        &depth_attachment_ref,           // pDepthStencilAttachment
        0,                               // preserveAttachmentCount
        nullptr                          // pPreserveAttachments
    };

    // Subpass dependencies for layout transitions
    std::array<VkSubpassDependency, 2> dependencies{
        {{
             // After timewarp samples from the offscreen image, it needs to be transitioned to a color attachment
             VK_SUBPASS_EXTERNAL,                           // srcSubpass
             0,                                             // dstSubpass
             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,         // srcStageMask
             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, // dstStageMask
             VK_ACCESS_SHADER_READ_BIT,                     // srcAccessMask
             VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,          // dstAccessMask
             VK_DEPENDENCY_BY_REGION_BIT                    // dependencyFlags
         },
         {
             // After the app is done rendering to the offscreen image, it needs to be transitioned to a shader read
             0,                                             // srcSubpass
             VK_SUBPASS_EXTERNAL,                           // dstSubpass
             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, // srcStageMask
             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,         // dstStageMask
             VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,          // srcAccessMask
             VK_ACCESS_SHADER_READ_BIT,                     // dstAccessMask
             VK_DEPENDENCY_BY_REGION_BIT                    // dependencyFlags
         }}};

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
    VK_ASSERT_SUCCESS(vkCreateRenderPass(display_sink_->vk_device, &render_pass_info, nullptr, &app_pass_))
}

void native_renderer::create_timewarp_pass() {
    std::array<VkAttachmentDescription, 1> attchment_descriptions{{{
        0,                                     // flags
        display_sink_->swapchain_image_format, // format
        VK_SAMPLE_COUNT_1_BIT,                 // samples
        VK_ATTACHMENT_LOAD_OP_CLEAR,           // loadOp
        VK_ATTACHMENT_STORE_OP_STORE,          // storeOp
        VK_ATTACHMENT_LOAD_OP_DONT_CARE,       // stencilLoadOp
        VK_ATTACHMENT_STORE_OP_DONT_CARE,      // stencilStoreOp
        VK_IMAGE_LAYOUT_UNDEFINED,             // initialLayout
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR        // finalLayout
    }}};

    VkAttachmentReference color_attachment_ref{
        0,                                       // attachment
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL // layout
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

    VkRenderPassCreateInfo render_pass_info{
        VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,            // sType
        nullptr,                                              // pNext
        0,                                                    // flags
        static_cast<uint32_t>(attchment_descriptions.size()), // attachmentCount
        attchment_descriptions.data(),                        // pAttachments
        1,                                                    // subpassCount
        &subpass,                                             // pSubpasses
        0,                                                    // dependencyCount
        nullptr                                               // pDependencies
    };

    VK_ASSERT_SUCCESS(vkCreateRenderPass(display_sink_->vk_device, &render_pass_info, nullptr, &timewarp_pass_))
}

PLUGIN_MAIN(native_renderer)
