#include <cassert>
#include <cstdint>
#include <opencv2/core/hal/interface.h>
#include <ratio>
#include <sys/types.h>
#include <vector>
#define VMA_IMPLEMENTATION
#include "common/data_format.hpp"
#include "common/error_util.hpp"
#include "common/global_module_defs.hpp"
#include "common/math_util.hpp"
#include "common/pose_prediction.hpp"
#include "common/switchboard.hpp"
#include "common/threadloop.hpp"
#include "common/vk_util/display_sink.hpp"
#include "common/vk_util/render_pass.hpp"
#include "common/threadloop.hpp"

#define TINYOBJLOADER_IMPLEMENTATION
#include "common/gl_util/lib/tiny_obj_loader.h"

#include <array>
#include <chrono>
#include <cmath>
#include <eigen3/Eigen/src/Core/Matrix.h>
#include <future>
#include <glm/detail/qualifier.hpp>
#include <glm/fwd.hpp>
#include <iostream>
#include <thread>
#include "unordered_map"
#include <vulkan/vulkan_core.h>

using namespace ILLIXR;

class native_renderer : public threadloop {
public:
    native_renderer(std::string name_, phonebook* pb)
        : threadloop{name_, pb}
        , sb{pb->lookup_impl<switchboard>()}
        , pp{pb->lookup_impl<pose_prediction>()}
        , ds{pb->lookup_impl<display_sink>()}
        , tw{pb->lookup_impl<timewarp>()}
        , src{pb->lookup_impl<app>()}
        , _m_clock{pb->lookup_impl<RelativeClock>()}
        , _m_vsync{sb->get_reader<switchboard::event_wrapper<time_point>>("vsync_estimate")}
        , last_fps_update{std::chrono::duration<long, std::nano>{0}} { }

    void _p_thread_setup() override {
        for (auto i = 0; i < 2; i++) {
            create_depth_image(&depth_images[i], &depth_image_allocations[i], &depth_image_views[i]);
        }
        for (auto i = 0; i < 2; i++) {
            create_offscreen_target(&offscreen_images[i], &offscreen_image_allocations[i], &offscreen_image_views[i], &offscreen_framebuffers[i]);
        }
        command_pool = vulkan_utils::create_command_pool(ds->vk_device, ds->graphics_queue_family);
        app_command_buffer = vulkan_utils::create_command_buffer(ds->vk_device, command_pool);
        timewarp_command_buffer = vulkan_utils::create_command_buffer(ds->vk_device, command_pool);
        create_sync_objects();
        create_app_pass();
        create_timewarp_pass();
        create_sync_objects();
        create_offscreen_framebuffers();
        create_swapchain_framebuffers();
        src->setup(app_pass, 0);
        tw->setup(timewarp_pass, 0, {std::vector{offscreen_image_views[0]}, std::vector{offscreen_image_views[1]}});
    }

    void _p_one_iteration() override {
        ds->poll_window_events();

        VK_ASSERT_SUCCESS(vkWaitForFences(ds->vk_device, 1, &frame_fence, VK_TRUE, UINT64_MAX));
        uint32_t swapchain_image_index;
        auto ret = (vkAcquireNextImageKHR(ds->vk_device, ds->vk_swapchain, UINT64_MAX, image_available_semaphore, VK_NULL_HANDLE, &swapchain_image_index));
        if (ret == VK_ERROR_OUT_OF_DATE_KHR || ret == VK_SUBOPTIMAL_KHR) {
            ds->recreate_swapchain();
            return;
        } else {
            VK_ASSERT_SUCCESS(ret);
        }
        VK_ASSERT_SUCCESS(vkResetFences(ds->vk_device, 1, &frame_fence));

        auto fast_pose = pp->get_fast_pose();
        src->update_uniforms(fast_pose.pose);
        VK_ASSERT_SUCCESS(vkResetCommandBuffer(app_command_buffer, 0));
        record_command_buffer(swapchain_image_index);

        const uint64_t ignored = 0;
        const uint64_t default_value = timeline_semaphore_value;
        const uint64_t fired_value = timeline_semaphore_value + 1;
        timeline_semaphore_value += 1;
        VkTimelineSemaphoreSubmitInfo timeline_submit_info = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
        timeline_submit_info.waitSemaphoreValueCount = 1;
        timeline_submit_info.pWaitSemaphoreValues = &ignored;
        timeline_submit_info.signalSemaphoreValueCount = 1;
        timeline_submit_info.pSignalSemaphoreValues = &fired_value;

        VkSubmitInfo submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        submit_info.waitSemaphoreCount = 1;
        submit_info.pWaitSemaphores = &image_available_semaphore;
        submit_info.pWaitDstStageMask = wait_stages;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &app_command_buffer;
        submit_info.signalSemaphoreCount = 1;
        submit_info.pSignalSemaphores = &app_render_finished_semaphore;
        submit_info.pNext = &timeline_submit_info;

        VK_ASSERT_SUCCESS(vkQueueSubmit(ds->graphics_queue, 1, &submit_info, nullptr));

        VkSemaphoreWaitInfo wait_info = { VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO };
        wait_info.semaphoreCount = 1;
        wait_info.pSemaphores = &app_render_finished_semaphore;
        wait_info.pValues = &fired_value;
        VK_ASSERT_SUCCESS(vkWaitSemaphores(ds->vk_device, &wait_info, UINT64_MAX));
        
        // for DRM, get vsync estimate
        std::this_thread::sleep_for(display_params::period / 6.0 * 5);

        tw->update_uniforms(fast_pose);
        VkSubmitInfo timewarp_submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        timewarp_submit_info.waitSemaphoreCount = 0;
        timewarp_submit_info.commandBufferCount = 1;
        timewarp_submit_info.pCommandBuffers = &timewarp_command_buffer;
        timewarp_submit_info.signalSemaphoreCount = 1;
        timewarp_submit_info.pSignalSemaphores = &timewarp_render_finished_semaphore;

        VK_ASSERT_SUCCESS(vkQueueSubmit(ds->graphics_queue, 1, &timewarp_submit_info, frame_fence));

        VkPresentInfoKHR present_info = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
        present_info.waitSemaphoreCount = 1;
        present_info.pWaitSemaphores = &timewarp_render_finished_semaphore;
        present_info.swapchainCount = 1;
        present_info.pSwapchains = &ds->vk_swapchain;
        present_info.pImageIndices = &swapchain_image_index;

        ret = (vkQueuePresentKHR(ds->graphics_queue, &present_info));
        if (ret == VK_ERROR_OUT_OF_DATE_KHR || ret == VK_SUBOPTIMAL_KHR) {
            ds->recreate_swapchain();
        } else {
            VK_ASSERT_SUCCESS(ret);
        }

// #ifndef NDEBUG
        if (_m_clock->now() - last_fps_update > std::chrono::milliseconds(1000)) {
            std::cout << "FPS: " << fps << std::endl;
            fps = 0;
            last_fps_update = _m_clock->now();
        } else {
            fps++;
        }
// #endif
    }
private:

    void recreate_swapchain() {
        vkDeviceWaitIdle(ds->vk_device);
        for (auto& framebuffer : swapchain_framebuffers) {
            vkDestroyFramebuffer(ds->vk_device, framebuffer, nullptr);
        }
        ds->recreate_swapchain();
        create_swapchain_framebuffers();
    }

    void create_swapchain_framebuffers() {
        swapchain_framebuffers.resize(ds->swapchain_image_views.size());
        for (auto i = 0; i < ds->swapchain_image_views.size(); i++) {
            std::array<VkImageView, 1> attachments = {
                ds->swapchain_image_views[i]
            };
            
            VkFramebufferCreateInfo framebuffer_info = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
            assert(timewarp_pass != VK_NULL_HANDLE);
            framebuffer_info.renderPass = timewarp_pass;
            framebuffer_info.attachmentCount = attachments.size();
            framebuffer_info.pAttachments = attachments.data();
            framebuffer_info.width = ds->swapchain_extent.width;
            framebuffer_info.height = ds->swapchain_extent.height;
            framebuffer_info.layers = 1;
            
            VK_ASSERT_SUCCESS(vkCreateFramebuffer(ds->vk_device, &framebuffer_info, nullptr, &swapchain_framebuffers[i]));
        }
    }

    void record_command_buffer(uint32_t swapchain_image_index) {
        VkCommandBufferBeginInfo begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        VK_ASSERT_SUCCESS(vkBeginCommandBuffer(app_command_buffer, &begin_info));

        for (auto eye = 0; eye < 2; eye++) {
            VkRenderPassBeginInfo render_pass_info = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
            assert(app_pass != VK_NULL_HANDLE);
            render_pass_info.renderPass = app_pass;
            render_pass_info.framebuffer = offscreen_framebuffers[eye];

            render_pass_info.renderArea.offset = { 0, 0 };
            render_pass_info.renderArea.extent = ds->swapchain_extent;

            std::array<VkClearValue, 2> clear_values = {};
            clear_values[0].color = { 1.0f, 1.0f, 1.0f, 1.0f };
            clear_values[1].depthStencil = { 1.0f, 0 };
            render_pass_info.clearValueCount = clear_values.size();
            render_pass_info.pClearValues = clear_values.data();

            vkCmdBeginRenderPass(app_command_buffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
            src->record_command_buffer(app_command_buffer, eye);
            vkCmdEndRenderPass(app_command_buffer);
        }
        VK_ASSERT_SUCCESS(vkEndCommandBuffer(app_command_buffer));

        VkCommandBufferBeginInfo timewarp_begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        VK_ASSERT_SUCCESS(vkBeginCommandBuffer(timewarp_command_buffer, &timewarp_begin_info));
        {
            VkRenderPassBeginInfo render_pass_info = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
            assert(timewarp_pass != VK_NULL_HANDLE);
            render_pass_info.renderPass = timewarp_pass;
            render_pass_info.framebuffer = swapchain_framebuffers[swapchain_image_index];
            
            render_pass_info.renderArea.offset = { 0, 0 };
            render_pass_info.renderArea.extent = ds->swapchain_extent;

            VkClearValue clear_value = {};
            clear_value.color = { 0.0f, 0.0f, 0.0f, 1.0f };
            render_pass_info.clearValueCount = 1;
            render_pass_info.pClearValues = &clear_value;
            
            vkCmdBeginRenderPass(timewarp_command_buffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

            for (auto eye = 0; eye < 2; eye++) {
                VkViewport viewport = {};
                viewport.x = ds->swapchain_extent.width / 2 * eye;
                viewport.y = 0.0f;
                viewport.width = ds->swapchain_extent.width;
                viewport.height = ds->swapchain_extent.height;
                viewport.minDepth = 0.0f;
                viewport.maxDepth = 1.0f;
                vkCmdSetViewport(timewarp_command_buffer, 0, 1, &viewport);

                VkRect2D scissor = {};
                scissor.offset = {0, 0};
                scissor.extent = ds->swapchain_extent;
                vkCmdSetScissor(timewarp_command_buffer, 0, 1, &scissor);

                tw->record_command_buffer(timewarp_command_buffer, 0, eye == 0);
            }
            vkCmdEndRenderPass(timewarp_command_buffer);   
        }
        VK_ASSERT_SUCCESS(vkEndCommandBuffer(timewarp_command_buffer));
    }

    void create_sync_objects() {
        VkSemaphoreTypeCreateInfo timeline_semaphore_info = { VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO };
        timeline_semaphore_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        timeline_semaphore_info.initialValue = 0;

        VkSemaphoreCreateInfo create_info = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        create_info.pNext = &timeline_semaphore_info;

        vkCreateSemaphore(ds->vk_device, &create_info, nullptr, &app_render_finished_semaphore);

        VkSemaphoreCreateInfo semaphore_info = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        VkFenceCreateInfo fence_info = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        
        VK_ASSERT_SUCCESS(vkCreateSemaphore(ds->vk_device, &semaphore_info, nullptr, &image_available_semaphore));
        VK_ASSERT_SUCCESS(vkCreateSemaphore(ds->vk_device, &semaphore_info, nullptr, &timewarp_render_finished_semaphore));
        VK_ASSERT_SUCCESS(vkCreateFence(ds->vk_device, &fence_info, nullptr, &frame_fence));
    }

    void create_depth_image(VkImage* depth_image, VmaAllocation* depth_image_allocation, VkImageView* depth_image_view) {
        VkImageCreateInfo image_info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        image_info.imageType = VK_IMAGE_TYPE_2D;
        image_info.format = VK_FORMAT_D32_SFLOAT;
        image_info.extent.width = display_params::width_pixels;
        image_info.extent.height = display_params::height_pixels;
        image_info.extent.depth = 1;
        image_info.mipLevels = 1;
        image_info.arrayLayers = 1;
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;
        image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

        VmaAllocationCreateInfo alloc_info = {};
        alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        VK_ASSERT_SUCCESS(vmaCreateImage(ds->vma_allocator, &image_info, &alloc_info, depth_image, depth_image_allocation, nullptr));

        VkImageViewCreateInfo view_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        view_info.image = *depth_image;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = VK_FORMAT_D32_SFLOAT;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        view_info.subresourceRange.baseMipLevel = 0;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount = 1;

        VK_ASSERT_SUCCESS(vkCreateImageView(ds->vk_device, &view_info, nullptr, depth_image_view));
    }

    void create_offscreen_target(VkImage* offscreen_image, VmaAllocation* offscreen_image_allocation, VkImageView* offscreen_image_view, VkFramebuffer* offscreen_framebuffer) {
        VkImageCreateInfo image_info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        image_info.imageType = VK_IMAGE_TYPE_2D;
        image_info.format = VK_FORMAT_B8G8R8A8_UNORM;
        image_info.extent.height = display_params::height_pixels;
        image_info.extent.width = display_params::width_pixels;
        image_info.extent.depth = 1;
        image_info.mipLevels = 1;
        image_info.arrayLayers = 1;
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;
        image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

        VmaAllocationCreateInfo alloc_info = {};
        alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        VK_ASSERT_SUCCESS(vmaCreateImage(ds->vma_allocator, &image_info, &alloc_info, offscreen_image, offscreen_image_allocation, nullptr));
        
        VkImageViewCreateInfo view_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        view_info.image = *offscreen_image;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = VK_FORMAT_B8G8R8A8_UNORM;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.baseMipLevel = 0;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount = 1;

        VK_ASSERT_SUCCESS(vkCreateImageView(ds->vk_device, &view_info, nullptr, offscreen_image_view));
    }

    void create_offscreen_framebuffers() {
        for (auto eye = 0; eye < 2; eye++) {
            std::array<VkImageView, 2> attachments = { offscreen_image_views[eye], depth_image_views[eye] };

            VkFramebufferCreateInfo framebuffer_info = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
            assert(app_pass != VK_NULL_HANDLE);
            framebuffer_info.renderPass = app_pass;
            framebuffer_info.attachmentCount = static_cast<uint32_t>(attachments.size());
            framebuffer_info.pAttachments = attachments.data();
            framebuffer_info.width = display_params::width_pixels;
            framebuffer_info.height = display_params::height_pixels;
            framebuffer_info.layers = 1;

            VK_ASSERT_SUCCESS(vkCreateFramebuffer(ds->vk_device, &framebuffer_info, nullptr, &offscreen_framebuffers[eye]));
        }
    }

    void create_app_pass() {
        std::array<VkAttachmentDescription, 2> attchmentDescriptions = {};
        attchmentDescriptions[0].format = VK_FORMAT_B8G8R8A8_UNORM;
        attchmentDescriptions[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attchmentDescriptions[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attchmentDescriptions[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attchmentDescriptions[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attchmentDescriptions[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attchmentDescriptions[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attchmentDescriptions[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        attchmentDescriptions[1].format = VK_FORMAT_D32_SFLOAT;
        attchmentDescriptions[1].samples = VK_SAMPLE_COUNT_1_BIT;
        attchmentDescriptions[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attchmentDescriptions[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attchmentDescriptions[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attchmentDescriptions[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attchmentDescriptions[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attchmentDescriptions[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference color_attachment_ref = {};
        color_attachment_ref.attachment = 0;
        color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depth_attachment_ref = {};
        depth_attachment_ref.attachment = 1;
        depth_attachment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.pColorAttachments = &color_attachment_ref;
        subpass.colorAttachmentCount = 1;
        subpass.pDepthStencilAttachment = &depth_attachment_ref;
        
        std::array<VkSubpassDependency, 2> dependencies = {};
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        VkRenderPassCreateInfo render_pass_info = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
        render_pass_info.attachmentCount = static_cast<uint32_t>(attchmentDescriptions.size());
        render_pass_info.pAttachments = attchmentDescriptions.data();
        render_pass_info.subpassCount = 1;
        render_pass_info.pSubpasses = &subpass;
        render_pass_info.dependencyCount = static_cast<uint32_t>(dependencies.size());
        render_pass_info.pDependencies = dependencies.data();

        VK_ASSERT_SUCCESS(vkCreateRenderPass(ds->vk_device, &render_pass_info, nullptr, &app_pass));
    }

    void create_timewarp_pass() {
        std::array<VkAttachmentDescription, 1> attchmentDescriptions = {};
        attchmentDescriptions[0].format = ds->swapchain_image_format;
        attchmentDescriptions[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attchmentDescriptions[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        attchmentDescriptions[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attchmentDescriptions[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attchmentDescriptions[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attchmentDescriptions[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attchmentDescriptions[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference color_attachment_ref = {};
        color_attachment_ref.attachment = 0;
        color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.pColorAttachments = &color_attachment_ref;
        subpass.colorAttachmentCount = 1;

        VkRenderPassCreateInfo render_pass_info = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
        render_pass_info.attachmentCount = static_cast<uint32_t>(attchmentDescriptions.size());
        render_pass_info.pAttachments = attchmentDescriptions.data();
        render_pass_info.subpassCount = 1;
        render_pass_info.pSubpasses = &subpass;

        VK_ASSERT_SUCCESS(vkCreateRenderPass(ds->vk_device, &render_pass_info, nullptr, &timewarp_pass));
    }

    const std::shared_ptr<switchboard>                                sb;
    const std::shared_ptr<pose_prediction>                            pp;
    const std::shared_ptr<display_sink>                               ds;
    const std::shared_ptr<timewarp>                                   tw;
    const std::shared_ptr<app>                                        src;
    const std::shared_ptr<const RelativeClock>                        _m_clock;
    const switchboard::reader<switchboard::event_wrapper<time_point>> _m_vsync;

    VkCommandPool command_pool;
    VkCommandBuffer app_command_buffer;
    VkCommandBuffer timewarp_command_buffer;

    std::array<VkImage, 2> depth_images;
    std::array<VmaAllocation, 2> depth_image_allocations;
    std::array<VkImageView, 2> depth_image_views;

    std::array<VkImage, 2> offscreen_images;
    std::array<VmaAllocation, 2> offscreen_image_allocations;
    std::array<VkImageView, 2> offscreen_image_views;
    std::array<VkFramebuffer, 2> offscreen_framebuffers;

    std::vector<VkFramebuffer> swapchain_framebuffers;

    VkRenderPass app_pass;
    VkRenderPass timewarp_pass;

    VkSemaphore image_available_semaphore;
    VkSemaphore app_render_finished_semaphore;
    VkSemaphore timewarp_render_finished_semaphore;
    VkFence frame_fence;

    uint64_t timeline_semaphore_value = 1;

    int fps;
    time_point last_fps_update;
};
PLUGIN_MAIN(native_renderer)
