#include "openwarp_vk.hpp"

#include "illixr/math_util.hpp"

using namespace ILLIXR;
using namespace ILLIXR::data_format;

openwarp_vk::openwarp_vk(const phonebook* pb)
    : phonebook_{pb}
    , switchboard_{phonebook_->lookup_impl<switchboard>()}
    , pose_prediction_{phonebook_->lookup_impl<pose_prediction>()}
    , disable_warp_{switchboard_->get_env_bool("ILLIXR_TIMEWARP_DISABLE", "False")} {
    if (switchboard_->get_env_char("ILLIXR_OPENWARP_WIDTH") == nullptr ||
        switchboard_->get_env_char("ILLIXR_OPENWARP_HEIGHT") == nullptr) {
        spdlog::get("illixr")->info("[openwarp] Grid dimensions not set, defaulting to 512x512");
        openwarp_width_  = 512;
        openwarp_height_ = 512;
    } else {
        openwarp_width_  = std::stoi(switchboard_->get_env_char("ILLIXR_OPENWARP_WIDTH"));
        openwarp_height_ = std::stoi(switchboard_->get_env_char("ILLIXR_OPENWARP_HEIGHT"));
    }

    using_godot_ = switchboard_->get_env_bool("ILLIXR_USING_GODOT");
    if (using_godot_)
        spdlog::get("illixr")->info("[openwarp] Using Godot projection matrices");
    else
        spdlog::get("illixr")->info("[openwarp] Using Unreal projection matrices");
}

// For objects that only need to be created a single time and do not need to change.
void openwarp_vk::initialize() {
    if (display_provider_->vma_allocator_) {
        this->vma_allocator_ = display_provider_->vma_allocator_;
    } else {
        this->vma_allocator_ = vulkan::create_vma_allocator(
            display_provider_->vk_instance_, display_provider_->vk_physical_device_, display_provider_->vk_device_);
        deletion_queue_.emplace([=]() {
            vmaDestroyAllocator(vma_allocator_);
        });
    }

    command_pool_   = vulkan::create_command_pool(display_provider_->vk_device_,
                                                  display_provider_->queues_[vulkan::queue::queue_type::GRAPHICS].family);
    command_buffer_ = vulkan::create_command_buffer(display_provider_->vk_device_, command_pool_);
    deletion_queue_.emplace([=]() {
        vkDestroyCommandPool(display_provider_->vk_device_, command_pool_, nullptr);
    });

    create_descriptor_set_layouts();
    create_uniform_buffers();
    create_texture_sampler();
}

void openwarp_vk::setup(VkRenderPass render_pass, uint32_t subpass,
                        std::shared_ptr<vulkan::buffer_pool<pose::fast_head_pose_type>> buffer_pool,
                        bool                                                            input_texture_external) {
    std::lock_guard<std::mutex> lock{setup_mutex_};

    display_provider_ = phonebook_->lookup_impl<vulkan::display_provider>();

    swapchain_width_  = display_provider_->swapchain_extent_.width == 0 ? display_params::width_pixels
                                                                        : display_provider_->swapchain_extent_.width;
    swapchain_height_ = display_provider_->swapchain_extent_.height == 0 ? display_params::height_pixels
                                                                         : display_provider_->swapchain_extent_.height;

    HMD::get_default_hmd_info(static_cast<int>(swapchain_width_), static_cast<int>(swapchain_height_),
                              display_params::width_meters, display_params::height_meters, display_params::lens_separation,
                              display_params::meters_per_tan_angle, display_params::aberration, hmd_info_);

    this->input_texture_external_ = input_texture_external;
    if (!initialized_) {
        initialize();
        initialized_ = true;
    } else {
        partial_destroy();
    }

    generate_openwarp_mesh(openwarp_width_, openwarp_height_);
    generate_distortion_data();

    create_vertex_buffers();
    create_index_buffers();

    this->buffer_pool_ = std::move(buffer_pool);

    create_descriptor_pool();
    create_openwarp_pipeline();
    distortion_correction_render_pass_ = render_pass;
    create_distortion_correction_pipeline(render_pass, subpass);

    create_offscreen_images();
    create_descriptor_sets();

    this->offloaded_rendering_ = switchboard_->get_env_bool("ILLIXR_OFFLOADING_RENDERING");
}

void openwarp_vk::partial_destroy() {
    vmaDestroyBuffer(vma_allocator_, ow_vertex_buffer_, ow_vertex_alloc_);
    vmaDestroyBuffer(vma_allocator_, dc_vertex_buffer_, dc_vertex_alloc_);
    vmaDestroyBuffer(vma_allocator_, ow_index_buffer_, ow_index_alloc_);
    vmaDestroyBuffer(vma_allocator_, dc_index_buffer_, dc_index_alloc_);

    for (size_t i = 0; i < offscreen_images_.size(); i++) {
        vkDestroyFramebuffer(display_provider_->vk_device_, offscreen_framebuffers_[i], nullptr);

        vkDestroyImageView(display_provider_->vk_device_, offscreen_image_views_[i], nullptr);
        vmaDestroyImage(vma_allocator_, offscreen_images_[i], offscreen_image_allocs_[i]);

        vkDestroyImageView(display_provider_->vk_device_, offscreen_depth_views_[i], nullptr);
        vmaDestroyImage(vma_allocator_, offscreen_depths_[i], offscreen_depth_allocs_[i]);
    }

    vkDestroyRenderPass(display_provider_->vk_device_, openwarp_render_pass_, nullptr);

    vkDestroyPipeline(display_provider_->vk_device_, openwarp_pipeline_, nullptr);
    openwarp_pipeline_ = VK_NULL_HANDLE;

    vkDestroyPipelineLayout(display_provider_->vk_device_, ow_pipeline_layout_, nullptr);
    ow_pipeline_layout_ = VK_NULL_HANDLE;

    vkDestroyPipeline(display_provider_->vk_device_, pipeline_, nullptr);
    pipeline_ = VK_NULL_HANDLE;

    vkDestroyPipelineLayout(display_provider_->vk_device_, dp_pipeline_layout_, nullptr);
    dp_pipeline_layout_ = VK_NULL_HANDLE;

    vkDestroyDescriptorPool(display_provider_->vk_device_, descriptor_pool_, nullptr);
    descriptor_pool_ = VK_NULL_HANDLE;
}

void openwarp_vk::update_uniforms(const BUFFER_TYPE& render_pose) {
    num_update_uniforms_calls_++;

    pose::head_pose_type latest_pose = disable_warp_ ? render_pose.pose : pose_prediction_->get_fast_pose().pose;

    for (int eye = 0; eye < 2; eye++) {
        Eigen::Matrix4f rendered_camera_matrix = create_camera_matrix(render_pose.pose, eye);
        Eigen::Matrix4f current_camera_matrix  = create_camera_matrix(latest_pose, eye);

        Eigen::Matrix4f warp_vp =
            basic_projection_[eye] * current_camera_matrix.inverse(); // inverse of camera matrix is view matrix

        auto* ow_ubo = (WarpMatrices*) ow_matrices_uniform_alloc_info_.pMappedData;
        memcpy(&ow_ubo->render_inv_projection[eye], inverse_projection_[eye].data(), sizeof(Eigen::Matrix4f));
        memcpy(&ow_ubo->render_inv_view[eye], rendered_camera_matrix.data(), sizeof(Eigen::Matrix4f));
        memcpy(&ow_ubo->warp_view_projection[eye], warp_vp.data(), sizeof(Eigen::Matrix4f));
    }
}

void openwarp_vk::record_command_buffer(VkCommandBuffer commandBuffer, VkFramebuffer framebuffer, int buffer_ind, bool left) {
    num_record_calls_++;

    if (left)
        frame_count_++;

    VkDeviceSize offsets = 0;
    VkClearValue clear_colors[2];
    clear_colors[0].color              = {0.0f, 0.0f, 0.0f, 1.0f};
    clear_colors[1].depthStencil.depth = rendering_params::reverse_z ? 0.0 : 1.0;

    // First render OpenWarp offscreen for a distortion correction pass later
    VkRenderPassBeginInfo ow_render_pass_info{};
    ow_render_pass_info.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    ow_render_pass_info.pNext           = nullptr;
    ow_render_pass_info.renderPass      = openwarp_render_pass_;
    ow_render_pass_info.framebuffer     = offscreen_framebuffers_[left ? 0 : 1];
    ow_render_pass_info.renderArea      = {{0, 0}, {static_cast<uint32_t>(swapchain_width_ / 2),
                                                    static_cast<uint32_t>(swapchain_height_)}};
    ow_render_pass_info.clearValueCount = 2;
    ow_render_pass_info.pClearValues    = clear_colors;

    VkViewport ow_viewport{};
    ow_viewport.x = 0;
    ow_viewport.y = 0;
    ow_viewport.width = static_cast<float>(swapchain_width_) / 2.f;
    ow_viewport.height = static_cast<float>(swapchain_height_);
    ow_viewport.minDepth = 0.0f;
    ow_viewport.maxDepth = 1.0f;

    VkRect2D ow_scissor{};
    ow_scissor.offset = {0, 0};
    ow_scissor.extent = {static_cast<uint32_t>(swapchain_width_ / 2), static_cast<uint32_t>(swapchain_height_)};

    auto eye = static_cast<uint32_t>(left ? 0 : 1);

    vkCmdBeginRenderPass(commandBuffer, &ow_render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdSetViewport(commandBuffer, 0, 1, &ow_viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &ow_scissor);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, openwarp_pipeline_);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &ow_vertex_buffer_, &offsets);
    vkCmdPushConstants(commandBuffer, ow_pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(uint32_t), &eye);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, ow_pipeline_layout_, 0, 1,
                            &ow_descriptor_sets_[!left][buffer_ind], 0, nullptr);
    vkCmdBindIndexBuffer(commandBuffer, ow_index_buffer_, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(commandBuffer, num_openwarp_indices_, 1, 0, 0, 0);
    vkCmdEndRenderPass(commandBuffer);

    // Then perform distortion correction to the framebuffer expected by Monado
    VkClearValue clear_color;
    clear_color.color = {0.0f, 0.0f, 0.0f, 1.0f};

    VkRenderPassBeginInfo dc_render_pass_info{};
    dc_render_pass_info.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    dc_render_pass_info.pNext           = nullptr;
    dc_render_pass_info.renderPass      = distortion_correction_render_pass_;
    dc_render_pass_info.framebuffer     = framebuffer;
    dc_render_pass_info.renderArea      = {{left ? 0 : static_cast<int32_t>(swapchain_width_ / 2), 0},
                                           {static_cast<uint32_t>(swapchain_width_ / 2),
                                            static_cast<uint32_t>(swapchain_height_)}};
    dc_render_pass_info.clearValueCount = 1;
    dc_render_pass_info.pClearValues    = &clear_color;

    VkViewport dc_viewport{};
    dc_viewport.x = left ? 0.f : static_cast<float>(swapchain_width_) / 2.f;
    dc_viewport.y = 0;
    dc_viewport.width = static_cast<float>(swapchain_width_) / 2.f;
    dc_viewport.height = static_cast<float>(swapchain_height_);
    dc_viewport.minDepth = 0.0f;
    dc_viewport.maxDepth = 1.0f;

    VkRect2D dc_scissor{};
    dc_scissor.offset = {left ? 0 : static_cast<int32_t>(swapchain_width_ / 2), 0};
    dc_scissor.extent = {static_cast<uint32_t>(swapchain_width_ / 2), static_cast<uint32_t>(swapchain_height_)};

    vkCmdBeginRenderPass(commandBuffer, &dc_render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdSetViewport(commandBuffer, 0, 1, &dc_viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &dc_scissor);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &dc_vertex_buffer_, &offsets);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, dp_pipeline_layout_, 0, 1,
                            &dp_descriptor_sets_[!left][0], 0, nullptr);
    vkCmdBindIndexBuffer(commandBuffer, dc_index_buffer_, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(commandBuffer, num_distortion_indices_, 1, 0, static_cast<int>(num_distortion_vertices_ * !left), 0);
    vkCmdEndRenderPass(commandBuffer);
}

bool openwarp_vk::is_external() {
    return false;
}

void openwarp_vk::destroy() {
    partial_destroy();
    // drain deletion_queue_
    while (!deletion_queue_.empty()) {
        deletion_queue_.top()();
        deletion_queue_.pop();
    }
}

void openwarp_vk::create_offscreen_images() {
    for (int eye = 0; eye < 2; eye++) {
        VkImageCreateInfo image_info{};
        image_info.sType                 = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image_info.pNext                 = nullptr;
        image_info.flags                 = 0;
        image_info.imageType             = VK_IMAGE_TYPE_2D;
        image_info.format                = VK_FORMAT_R8G8B8A8_UNORM;
        image_info.extent                = {static_cast<uint32_t>(swapchain_width_ / 2),
                                            static_cast<uint32_t>(swapchain_height_), 1};
        image_info.mipLevels             = 1;
        image_info.arrayLayers           = 1;
        image_info.samples               = VK_SAMPLE_COUNT_1_BIT;
        image_info.tiling                = VK_IMAGE_TILING_OPTIMAL;
        image_info.usage                 = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        image_info.sharingMode           = {};
        image_info.queueFamilyIndexCount = 0;
        image_info.pQueueFamilyIndices   = nullptr;
        image_info.initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo create_info = {};
        create_info.flags          = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
        create_info.usage          = VMA_MEMORY_USAGE_AUTO;
        create_info.requiredFlags  = {};
        create_info.preferredFlags = {};
        create_info.memoryTypeBits = {};
        create_info.pool           = {};
        create_info.pUserData      = nullptr;
        create_info.priority       = 1.0f;

        VK_ASSERT_SUCCESS(vmaCreateImage(vma_allocator_, &image_info, &create_info, &offscreen_images_[eye],
                                         &offscreen_image_allocs_[eye], nullptr))

        VkImageViewCreateInfo view_info = {};
        view_info.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.pNext            = nullptr;
        view_info.flags            = {};
        view_info.image            = offscreen_images_[eye];
        view_info.viewType         = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format           = VK_FORMAT_R8G8B8A8_UNORM;
        view_info.components       = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                                      VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
        view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        VK_ASSERT_SUCCESS(vkCreateImageView(display_provider_->vk_device_, &view_info, nullptr, &offscreen_image_views_[eye]))

        VkImageCreateInfo depth_image_info{};
        depth_image_info.sType                 = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        depth_image_info.pNext                 = nullptr;
        depth_image_info.flags                 = {};
        depth_image_info.imageType             = VK_IMAGE_TYPE_2D;
        depth_image_info.format                = VK_FORMAT_D16_UNORM;
        depth_image_info.extent                = {static_cast<uint32_t>(swapchain_width_ / 2), static_cast<uint32_t>(swapchain_height_), 1};
        depth_image_info.mipLevels             = 1;
        depth_image_info.arrayLayers           = 1;
        depth_image_info.samples               = VK_SAMPLE_COUNT_1_BIT;
        depth_image_info.tiling                = VK_IMAGE_TILING_OPTIMAL;
        depth_image_info.usage                 = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        depth_image_info.sharingMode           = {};
        depth_image_info.queueFamilyIndexCount = 0;
        depth_image_info.pQueueFamilyIndices   = nullptr;
        depth_image_info.initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo depth_create_info = {};
        depth_create_info.flags          = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
        depth_create_info.usage          = VMA_MEMORY_USAGE_AUTO;
        depth_create_info.requiredFlags  = {};
        depth_create_info.preferredFlags = {};
        depth_create_info.memoryTypeBits = 0;
        depth_create_info.pool           = {};
        depth_create_info.pUserData      = nullptr;
        depth_create_info.priority       = 1.0f;

        VK_ASSERT_SUCCESS(vmaCreateImage(vma_allocator_, &depth_image_info, &depth_create_info, &offscreen_depths_[eye],
                                         &offscreen_depth_allocs_[eye], nullptr))

        VkImageViewCreateInfo depth_view_info = {};
        depth_view_info.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        depth_view_info.pNext            = nullptr;
        depth_view_info.flags            = {};
        depth_view_info.image            = offscreen_depths_[eye];
        depth_view_info.viewType         = VK_IMAGE_VIEW_TYPE_2D;
        depth_view_info.format           = VK_FORMAT_D16_UNORM;
        depth_view_info.components       = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                                            VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
        depth_view_info.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};

        VK_ASSERT_SUCCESS(
            vkCreateImageView(display_provider_->vk_device_, &depth_view_info, nullptr, &offscreen_depth_views_[eye]))

        VkImageView attachments[2] = {offscreen_image_views_[eye], offscreen_depth_views_[eye]};

        // Need a framebuffer to render to
        VkFramebufferCreateInfo framebuffer_info = {};
        framebuffer_info.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_info.pNext           = nullptr;
        framebuffer_info.flags           = 0;
        framebuffer_info.renderPass      = openwarp_render_pass_;
        framebuffer_info.attachmentCount = 2;
        framebuffer_info.pAttachments    = attachments;
        framebuffer_info.width           = static_cast<uint32_t>(swapchain_width_ / 2);
        framebuffer_info.height          = static_cast<uint32_t>(swapchain_height_);
        framebuffer_info.layers          = 1;

        VK_ASSERT_SUCCESS(
            vkCreateFramebuffer(display_provider_->vk_device_, &framebuffer_info, nullptr, &offscreen_framebuffers_[eye]))
    }
}

void openwarp_vk::create_vertex_buffers() {
    // OpenWarp Vertices
    VkBufferCreateInfo ow_staging_buffer_info = {};
    ow_staging_buffer_info.sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ow_staging_buffer_info.pNext                 = nullptr;
    ow_staging_buffer_info.flags                 = {};
    ow_staging_buffer_info.size                  = sizeof(OpenWarpVertex) * num_openwarp_vertices_;
    ow_staging_buffer_info.usage                 = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    ow_staging_buffer_info.sharingMode           = {};
    ow_staging_buffer_info.queueFamilyIndexCount = 0;
    ow_staging_buffer_info.pQueueFamilyIndices   = nullptr;

    VmaAllocationCreateInfo ow_staging_alloc_info = {};
    ow_staging_alloc_info.usage                   = VMA_MEMORY_USAGE_AUTO;
    ow_staging_alloc_info.flags                   = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

    VkBuffer      ow_staging_buffer;
    VmaAllocation ow_staging_alloc;
    VK_ASSERT_SUCCESS(vmaCreateBuffer(vma_allocator_, &ow_staging_buffer_info, &ow_staging_alloc_info, &ow_staging_buffer,
                                      &ow_staging_alloc, nullptr))

    VkBufferCreateInfo ow_buffer_info = {};
    ow_buffer_info.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ow_buffer_info.pNext       = nullptr;
    ow_buffer_info.flags       = {};
    ow_buffer_info.size        = sizeof(OpenWarpVertex) * num_openwarp_vertices_;
    ow_buffer_info.usage       = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    ow_buffer_info.sharingMode = {};
    ow_buffer_info.queueFamilyIndexCount = 0;
    ow_buffer_info.pQueueFamilyIndices   = nullptr;

    VmaAllocationCreateInfo ow_alloc_info = {};
    ow_alloc_info.usage                   = VMA_MEMORY_USAGE_GPU_ONLY;

    VK_ASSERT_SUCCESS(
        vmaCreateBuffer(vma_allocator_, &ow_buffer_info, &ow_alloc_info, &ow_vertex_buffer_, &ow_vertex_alloc_, nullptr))

    void* ow_mapped_data;
    VK_ASSERT_SUCCESS(vmaMapMemory(vma_allocator_, ow_staging_alloc, &ow_mapped_data))
    memcpy(ow_mapped_data, openwarp_vertices_.data(), sizeof(OpenWarpVertex) * num_openwarp_vertices_);
    vmaUnmapMemory(vma_allocator_, ow_staging_alloc);

    VkCommandBuffer ow_command_buffer_local = vulkan::begin_one_time_command(display_provider_->vk_device_, command_pool_);
    VkBufferCopy    ow_copy_region          = {};
    ow_copy_region.size                     = sizeof(OpenWarpVertex) * num_openwarp_vertices_;
    vkCmdCopyBuffer(ow_command_buffer_local, ow_staging_buffer, ow_vertex_buffer_, 1, &ow_copy_region);
    vulkan::end_one_time_command(display_provider_->vk_device_, command_pool_,
                                 display_provider_->queues_[vulkan::queue::queue_type::GRAPHICS], ow_command_buffer_local);

    vmaDestroyBuffer(vma_allocator_, ow_staging_buffer, ow_staging_alloc);

    // Distortion Correction Vertices
    VkBufferCreateInfo dc_staging_buffer_info = {};
    dc_staging_buffer_info.sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    dc_staging_buffer_info.pNext                 = nullptr;
    dc_staging_buffer_info.flags                 = {};
    dc_staging_buffer_info.size                  = sizeof(OpenWarpVertex) * num_openwarp_vertices_;
    dc_staging_buffer_info.usage                 = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    dc_staging_buffer_info.sharingMode           = {};
    dc_staging_buffer_info.queueFamilyIndexCount = 0;
    dc_staging_buffer_info.pQueueFamilyIndices   = nullptr;

    VmaAllocationCreateInfo dc_staging_alloc_info = {};
    dc_staging_alloc_info.usage                   = VMA_MEMORY_USAGE_AUTO;
    dc_staging_alloc_info.flags                   = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

    VkBuffer      dc_staging_buffer;
    VmaAllocation dc_staging_alloc;
    VK_ASSERT_SUCCESS(vmaCreateBuffer(vma_allocator_, &dc_staging_buffer_info, &dc_staging_alloc_info, &dc_staging_buffer,
                                      &dc_staging_alloc, nullptr))

    VkBufferCreateInfo dc_buffer_info = {};
    dc_buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    dc_buffer_info.pNext = nullptr;
    dc_buffer_info.flags = {};
    dc_buffer_info.size  = sizeof(DistortionCorrectionVertex) * num_distortion_vertices_ * HMD::NUM_EYES;
    dc_buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    dc_buffer_info.sharingMode           = {};
    dc_buffer_info.queueFamilyIndexCount = 0;
    dc_buffer_info.pQueueFamilyIndices   = nullptr;
    dc_buffer_info.size               = sizeof(DistortionCorrectionVertex) * num_distortion_vertices_ * HMD::NUM_EYES;

    VmaAllocationCreateInfo dc_alloc_info = {};
    dc_alloc_info.usage                   = VMA_MEMORY_USAGE_GPU_ONLY;

    VK_ASSERT_SUCCESS(
        vmaCreateBuffer(vma_allocator_, &dc_buffer_info, &dc_alloc_info, &dc_vertex_buffer_, &dc_vertex_alloc_, nullptr))

    void* dc_mapped_data;
    VK_ASSERT_SUCCESS(vmaMapMemory(vma_allocator_, dc_staging_alloc, &dc_mapped_data))
    memcpy(dc_mapped_data, distortion_vertices_.data(),
           sizeof(DistortionCorrectionVertex) * num_distortion_vertices_ * HMD::NUM_EYES);
    vmaUnmapMemory(vma_allocator_, dc_staging_alloc);

    VkCommandBuffer dc_command_buffer_local = vulkan::begin_one_time_command(display_provider_->vk_device_, command_pool_);
    VkBufferCopy    dc_copy_region          = {};
    dc_copy_region.size                     = sizeof(DistortionCorrectionVertex) * num_distortion_vertices_ * HMD::NUM_EYES;
    vkCmdCopyBuffer(dc_command_buffer_local, dc_staging_buffer, dc_vertex_buffer_, 1, &dc_copy_region);
    vulkan::end_one_time_command(display_provider_->vk_device_, command_pool_,
                                 display_provider_->queues_[vulkan::queue::queue_type::GRAPHICS], dc_command_buffer_local);

    vmaDestroyBuffer(vma_allocator_, dc_staging_buffer, dc_staging_alloc);
}

void openwarp_vk::create_index_buffers() {
    // OpenWarp index buffer
    VkBufferCreateInfo ow_staging_buffer_info = {};
    ow_staging_buffer_info.sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ow_staging_buffer_info.pNext                 = nullptr;
    ow_staging_buffer_info.flags                 = {};
    ow_staging_buffer_info.size                  = sizeof(uint32_t) * num_openwarp_indices_;
    ow_staging_buffer_info.usage                 = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    ow_staging_buffer_info.sharingMode           = {};
    ow_staging_buffer_info.queueFamilyIndexCount = 0;
    ow_staging_buffer_info.pQueueFamilyIndices   = nullptr;

    VmaAllocationCreateInfo ow_staging_alloc_info = {};
    ow_staging_alloc_info.usage                   = VMA_MEMORY_USAGE_AUTO;
    ow_staging_alloc_info.flags                   = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

    VkBuffer      ow_staging_buffer;
    VmaAllocation ow_staging_alloc;
    VK_ASSERT_SUCCESS(vmaCreateBuffer(vma_allocator_, &ow_staging_buffer_info, &ow_staging_alloc_info, &ow_staging_buffer,
                                      &ow_staging_alloc, nullptr))

    VkBufferCreateInfo ow_buffer_info = {};
    ow_staging_buffer_info.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ow_staging_buffer_info.pNext       = nullptr;
    ow_staging_buffer_info.flags       = {};
    ow_staging_buffer_info.size        = sizeof(uint32_t) * num_openwarp_indices_;
    ow_staging_buffer_info.usage       = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    ow_staging_buffer_info.sharingMode = {};
    ow_staging_buffer_info.queueFamilyIndexCount = 0;
    ow_staging_buffer_info.pQueueFamilyIndices   = nullptr;

    VmaAllocationCreateInfo ow_alloc_info = {};
    ow_alloc_info.usage                   = VMA_MEMORY_USAGE_GPU_ONLY;

    VK_ASSERT_SUCCESS(
        vmaCreateBuffer(vma_allocator_, &ow_buffer_info, &ow_alloc_info, &ow_index_buffer_, &ow_index_alloc_, nullptr))

    void* ow_mapped_data;
    VK_ASSERT_SUCCESS(vmaMapMemory(vma_allocator_, ow_staging_alloc, &ow_mapped_data))
    memcpy(ow_mapped_data, openwarp_indices_.data(), sizeof(uint32_t) * num_openwarp_indices_);
    vmaUnmapMemory(vma_allocator_, ow_staging_alloc);

    VkCommandBuffer ow_command_buffer_local = vulkan::begin_one_time_command(display_provider_->vk_device_, command_pool_);
    VkBufferCopy    ow_copy_region          = {};
    ow_copy_region.size                     = sizeof(uint32_t) * num_openwarp_indices_;
    vkCmdCopyBuffer(ow_command_buffer_local, ow_staging_buffer, ow_index_buffer_, 1, &ow_copy_region);
    vulkan::end_one_time_command(display_provider_->vk_device_, command_pool_,
                                 display_provider_->queues_[vulkan::queue::queue_type::GRAPHICS], ow_command_buffer_local);

    vmaDestroyBuffer(vma_allocator_, ow_staging_buffer, ow_staging_alloc);

    // Distortion correction index buffer
    VkBufferCreateInfo dc_staging_buffer_info = {};
    dc_staging_buffer_info.sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    dc_staging_buffer_info.pNext                 = nullptr;
    dc_staging_buffer_info.flags                 = {};
    dc_staging_buffer_info.size                  = sizeof(uint32_t) * num_distortion_indices_;
    dc_staging_buffer_info.usage                 = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    dc_staging_buffer_info.sharingMode           = {};
    dc_staging_buffer_info.queueFamilyIndexCount = 0;
    dc_staging_buffer_info.pQueueFamilyIndices   = nullptr;

    VmaAllocationCreateInfo dc_staging_alloc_info = {};
    dc_staging_alloc_info.usage                   = VMA_MEMORY_USAGE_AUTO;
    dc_staging_alloc_info.flags                   = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

    VkBuffer      dc_staging_buffer;
    VmaAllocation dc_staging_alloc;
    VK_ASSERT_SUCCESS(vmaCreateBuffer(vma_allocator_, &dc_staging_buffer_info, &dc_staging_alloc_info, &dc_staging_buffer,
                                      &dc_staging_alloc, nullptr))

    VkBufferCreateInfo dc_buffer_info = {};
    dc_buffer_info.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    dc_buffer_info.pNext       = nullptr;
    dc_buffer_info.flags       = {};
    dc_buffer_info.size        = sizeof(uint32_t) * num_distortion_indices_;
    dc_buffer_info.usage       = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    dc_buffer_info.sharingMode = {};
    dc_buffer_info.queueFamilyIndexCount = 0;
    dc_buffer_info.pQueueFamilyIndices   = nullptr;

    VmaAllocationCreateInfo dc_alloc_info = {};
    dc_alloc_info.usage                   = VMA_MEMORY_USAGE_GPU_ONLY;

    VK_ASSERT_SUCCESS(
        vmaCreateBuffer(vma_allocator_, &dc_buffer_info, &dc_alloc_info, &dc_index_buffer_, &dc_index_alloc_, nullptr))

    void* dc_mapped_data;
    VK_ASSERT_SUCCESS(vmaMapMemory(vma_allocator_, dc_staging_alloc, &dc_mapped_data))
    memcpy(dc_mapped_data, distortion_indices_.data(), sizeof(uint32_t) * num_distortion_indices_);
    vmaUnmapMemory(vma_allocator_, dc_staging_alloc);

    VkCommandBuffer dc_command_buffer_local = vulkan::begin_one_time_command(display_provider_->vk_device_, command_pool_);
    VkBufferCopy    dc_copy_region          = {};
    dc_copy_region.size                     = sizeof(uint32_t) * num_distortion_indices_;
    vkCmdCopyBuffer(dc_command_buffer_local, dc_staging_buffer, dc_index_buffer_, 1, &dc_copy_region);
    vulkan::end_one_time_command(display_provider_->vk_device_, command_pool_,
                                 display_provider_->queues_[vulkan::queue::queue_type::GRAPHICS], dc_command_buffer_local);

    vmaDestroyBuffer(vma_allocator_, dc_staging_buffer, dc_staging_alloc);
}

void openwarp_vk::generate_distortion_data() {
    // Calculate the number of vertices+ineye_tiles_high distortion mesh.
    num_distortion_vertices_ = (hmd_info_.eye_tiles_high + 1) * (hmd_info_.eye_tiles_wide + 1);
    num_distortion_indices_  = hmd_info_.eye_tiles_high * hmd_info_.eye_tiles_wide * 6;

    // Allocate memory for the elements/indices array.
    distortion_indices_.resize(num_distortion_indices_);

    // This is just a simple grid/plane index array, nothing fancy.
    // Same for both eye distortions, too!
    for (int y = 0; y < hmd_info_.eye_tiles_high; y++) {
        for (int x = 0; x < hmd_info_.eye_tiles_wide; x++) {
            const int offset = (y * hmd_info_.eye_tiles_wide + x) * 6;

            distortion_indices_[offset + 0] = ((y + 0) * (hmd_info_.eye_tiles_wide + 1) + (x + 0));
            distortion_indices_[offset + 1] = ((y + 1) * (hmd_info_.eye_tiles_wide + 1) + (x + 0));
            distortion_indices_[offset + 2] = ((y + 0) * (hmd_info_.eye_tiles_wide + 1) + (x + 1));

            distortion_indices_[offset + 3] = ((y + 0) * (hmd_info_.eye_tiles_wide + 1) + (x + 1));
            distortion_indices_[offset + 4] = ((y + 1) * (hmd_info_.eye_tiles_wide + 1) + (x + 0));
            distortion_indices_[offset + 5] = ((y + 1) * (hmd_info_.eye_tiles_wide + 1) + (x + 1));
        }
    }

    // There are `num_distortion_vertices_` distortion coordinates for each color channel (3) of each eye (2).
    // These are NOT the coordinates of the distorted vertices. They are *coefficients* that will be used to
    // offset the UV coordinates of the distortion mesh.
    std::array<std::array<std::vector<HMD::mesh_coord2d_t>, HMD::NUM_COLOR_CHANNELS>, HMD::NUM_EYES> distort_coords;
    for (auto& eye_coords : distort_coords) {
        for (auto& channel_coords : eye_coords) {
            channel_coords.resize(num_distortion_vertices_);
        }
    }
    HMD::build_distortion_meshes(distort_coords, hmd_info_);

    // Allocate memory for position and UV CPU buffers.
    const std::size_t num_elems_pos_uv = HMD::NUM_EYES * num_distortion_vertices_;
    distortion_vertices_.resize(num_elems_pos_uv);

    // Construct perspective projection matrices
    for (int eye = 0; eye < 2; eye++) {
        if (!offloaded_rendering_) {
            if (!using_godot_) {
                math_util::unreal_projection(&basic_projection_[eye], index_params::fov_left[eye], index_params::fov_right[eye],
                                             index_params::fov_up[eye], index_params::fov_down[eye]);
            } else {
                math_util::godot_projection(&basic_projection_[eye], index_params::fov_left[eye], index_params::fov_right[eye],
                                            index_params::fov_up[eye], index_params::fov_down[eye]);
            }

            inverse_projection_[eye] = basic_projection_[eye].inverse();
        } else {
            float scale = 1.0f;
            if (switchboard_->get_env_char("ILLIXR_OVERSCAN") != nullptr) {
                scale = std::stof(switchboard_->get_env_char("ILLIXR_OVERSCAN"));
            }
            float fov_left  = scale * server_params::fov_left[eye];
            float fov_right = scale * server_params::fov_right[eye];
            float fov_up    = scale * server_params::fov_up[eye];
            float fov_down  = scale * server_params::fov_down[eye];

            // The server can render at a larger FoV, so the inverse should account for that.
            // The FOVs provided to the server should match the ones provided to Monado.
            Eigen::Matrix4f l_server_fov;
            if (!using_godot_) {
                math_util::unreal_projection(&l_server_fov, fov_left, fov_right, fov_up, fov_down);
            } else {
                math_util::godot_projection(&l_server_fov, fov_left, fov_right, fov_up, fov_down);
            }

            inverse_projection_[eye] = l_server_fov.inverse();
        }
    }

    for (int eye = 0; eye < HMD::NUM_EYES; eye++) {
        Eigen::Matrix4f distortion_matrix = calculate_distortion_transform(basic_projection_[eye]);
        for (int y = 0; y <= hmd_info_.eye_tiles_high; y++) {
            for (int x = 0; x <= hmd_info_.eye_tiles_wide; x++) {
                const int index = y * (hmd_info_.eye_tiles_wide + 1) + x;

                // Set the physical distortion mesh coordinates. These are rectangular/gridlike, not distorted.
                // The distortion is handled by the UVs, not the actual mesh coordinates!
                distortion_vertices_[eye * num_distortion_vertices_ + index].pos.x =
                    (-1.0f + 2 * (static_cast<float>(x) / static_cast<float>(hmd_info_.eye_tiles_wide)));

                distortion_vertices_[eye * num_distortion_vertices_ + index].pos.y = (input_texture_external_ ? 1.0f : -1.0f) *
                    (-1.0f +
                     2.0f * (static_cast<float>(hmd_info_.eye_tiles_high - y) / static_cast<float>(hmd_info_.eye_tiles_high)) *
                         (static_cast<float>(hmd_info_.eye_tiles_high * hmd_info_.tile_pixels_high) /
                          static_cast<float>(hmd_info_.display_pixels_high)));

                distortion_vertices_[eye * num_distortion_vertices_ + index].pos.z = 0.0f;

                // Use the previously-calculated distort_coords to set the UVs on the distortion mesh
                Eigen::Vector4f vertex_uv0(distort_coords[eye][0][index].x, distort_coords[eye][0][index].y, -1, 1);
                Eigen::Vector4f vertex_uv1(distort_coords[eye][1][index].x, distort_coords[eye][1][index].y, -1, 1);
                Eigen::Vector4f vertex_uv2(distort_coords[eye][2][index].x, distort_coords[eye][2][index].y, -1, 1);

                Eigen::Vector4f uv0 = distortion_matrix * vertex_uv0;
                Eigen::Vector4f uv1 = distortion_matrix * vertex_uv1;
                Eigen::Vector4f uv2 = distortion_matrix * vertex_uv2;

                float factor0 = 1.0f / std::max(uv0.z(), 0.00001f);
                float factor1 = 1.0f / std::max(uv1.z(), 0.00001f);
                float factor2 = 1.0f / std::max(uv2.z(), 0.00001f);

                distortion_vertices_[eye * num_distortion_vertices_ + index].uv0.x = uv0.x() * factor0;
                distortion_vertices_[eye * num_distortion_vertices_ + index].uv0.y = uv0.y() * factor0;
                distortion_vertices_[eye * num_distortion_vertices_ + index].uv1.x = uv1.x() * factor1;
                distortion_vertices_[eye * num_distortion_vertices_ + index].uv1.y = uv1.y() * factor1;
                distortion_vertices_[eye * num_distortion_vertices_ + index].uv2.x = uv2.x() * factor2;
                distortion_vertices_[eye * num_distortion_vertices_ + index].uv2.y = uv2.y() * factor2;
            }
        }
    }
}

void openwarp_vk::generate_openwarp_mesh(size_t width, size_t height) {
    spdlog::get("illixr")->info("[openwarp] Generating reprojection mesh with resolution ({}, {})", width, height);

    // width and height are not in # of verts, but in # of faces.
    num_openwarp_indices_  = static_cast<uint32_t>(2 * 3 * width * height);
    num_openwarp_vertices_ = static_cast<uint32_t>((width + 1) * (height + 1));

    // Size the vectors accordingly
    openwarp_indices_.resize(num_openwarp_indices_);
    openwarp_vertices_.resize(num_openwarp_vertices_);

    // Build indices.
    for (size_t y = 0; y < height; y++) {
        for (size_t x = 0; x < width; x++) {
            const size_t offset = (y * width + x) * 6;

            openwarp_indices_[offset + 0] = (GLuint) ((y + 0) * (width + 1) + (x + 0));
            openwarp_indices_[offset + 1] = (GLuint) ((y + 1) * (width + 1) + (x + 0));
            openwarp_indices_[offset + 2] = (GLuint) ((y + 0) * (width + 1) + (x + 1));

            openwarp_indices_[offset + 3] = (GLuint) ((y + 0) * (width + 1) + (x + 1));
            openwarp_indices_[offset + 4] = (GLuint) ((y + 1) * (width + 1) + (x + 0));
            openwarp_indices_[offset + 5] = (GLuint) ((y + 1) * (width + 1) + (x + 1));
        }
    }

    // Build vertices
    for (size_t y = 0; y < height + 1; y++) {
        for (size_t x = 0; x < width + 1; x++) {
            size_t index = y * (width + 1) + x;

            openwarp_vertices_[index].uv.x = static_cast<float>(x) / static_cast<float>(width);
            openwarp_vertices_[index].uv.y = (static_cast<float>(height) - static_cast<float>(y)) / static_cast<float>(height);

            if (x == 0) {
                openwarp_vertices_[index].uv.x = -0.5f;
            }
            if (x == width) {
                openwarp_vertices_[index].uv.x = 1.5f;
            }

            if (y == 0) {
                openwarp_vertices_[index].uv.y = 1.5f;
            }
            if (y == height) {
                openwarp_vertices_[index].uv.y = -0.5f;
            }
        }
    }
}

void openwarp_vk::create_texture_sampler() {
    VkSamplerCreateInfo sampler_info = {};
    sampler_info.sType     = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.pNext     = nullptr;
    sampler_info.flags     = {};
    sampler_info.magFilter = VK_FILTER_LINEAR; // how to interpolate texels that are magnified on screen
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;

    sampler_info.mipLodBias       = 0.f;
    sampler_info.anisotropyEnable = VK_FALSE;
    sampler_info.maxAnisotropy    = 0.f;
    sampler_info.compareEnable    = VK_FALSE;
    sampler_info.compareOp        = VK_COMPARE_OP_ALWAYS;
    sampler_info.minLod           = 0.f;
    sampler_info.maxLod           = 0.f;
    sampler_info.borderColor      = VK_BORDER_COLOR_INT_OPAQUE_BLACK; // black outside the texture
    sampler_info.unnormalizedCoordinates = VK_FALSE;

    VK_ASSERT_SUCCESS(vkCreateSampler(display_provider_->vk_device_, &sampler_info, nullptr, &fb_sampler_))
    deletion_queue_.emplace([=]() {
        vkDestroySampler(display_provider_->vk_device_, fb_sampler_, nullptr);
    });
}

void openwarp_vk::create_descriptor_set_layouts() {
    // OpenWarp descriptor set
    VkDescriptorSetLayoutBinding image_layout_binding = {};
    image_layout_binding.binding            = 0;
    image_layout_binding.descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    image_layout_binding.descriptorCount    = 1;
    image_layout_binding.stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT;
    image_layout_binding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding depth_layout_binding = {};
    depth_layout_binding.binding         = 1;
    depth_layout_binding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    depth_layout_binding.descriptorCount = 1;
    depth_layout_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    // depth_layout_binding.stageFlags                   = VK_SHADER_STAGE_VERTEX_BIT;
    depth_layout_binding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding matrix_ubo_layout_binding = {};
    matrix_ubo_layout_binding.binding            = 2;
    matrix_ubo_layout_binding.descriptorType     = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    matrix_ubo_layout_binding.descriptorCount    = 1;
    matrix_ubo_layout_binding.stageFlags         = VK_SHADER_STAGE_VERTEX_BIT;
    matrix_ubo_layout_binding.pImmutableSamplers = nullptr;

    std::array<VkDescriptorSetLayoutBinding, 3> ow_bindings    = {image_layout_binding, depth_layout_binding,
                                                                  matrix_ubo_layout_binding};
    VkDescriptorSetLayoutCreateInfo             ow_layout_info = {};
    ow_layout_info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ow_layout_info.pNext        = nullptr;
    ow_layout_info.flags        = {};
    ow_layout_info.bindingCount = static_cast<uint32_t>(ow_bindings.size());
    ow_layout_info.pBindings    = ow_bindings.data(); // array of VkDescriptorSetLayoutBinding structs


    VK_ASSERT_SUCCESS(
        vkCreateDescriptorSetLayout(display_provider_->vk_device_, &ow_layout_info, nullptr, &ow_descriptor_set_layout_))
    deletion_queue_.emplace([=]() {
        vkDestroyDescriptorSetLayout(display_provider_->vk_device_, ow_descriptor_set_layout_, nullptr);
    });

    // Distortion correction descriptor set
    VkDescriptorSetLayoutBinding offscreen_image_layout_binding = {};
    offscreen_image_layout_binding.binding            = 0; // binding number in the shader
    offscreen_image_layout_binding.descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    offscreen_image_layout_binding.descriptorCount    = 1;
    offscreen_image_layout_binding.stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT; // shader stages that can access the descriptor
    offscreen_image_layout_binding.pImmutableSamplers = nullptr;

    std::array<VkDescriptorSetLayoutBinding, 1> dc_bindings    = {offscreen_image_layout_binding};
    VkDescriptorSetLayoutCreateInfo             dc_layout_info = {};
    dc_layout_info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dc_layout_info.pNext        = nullptr;
    dc_layout_info.flags        = {};
    dc_layout_info.bindingCount = static_cast<uint32_t>(dc_bindings.size());
    dc_layout_info.pBindings    = dc_bindings.data(); // array of VkDescriptorSetLayoutBinding structs

    VK_ASSERT_SUCCESS(
        vkCreateDescriptorSetLayout(display_provider_->vk_device_, &dc_layout_info, nullptr, &dp_descriptor_set_layout_))
    deletion_queue_.emplace([=]() {
        vkDestroyDescriptorSetLayout(display_provider_->vk_device_, dp_descriptor_set_layout_, nullptr);
    });
}

void openwarp_vk::create_uniform_buffers() {
    // Matrix data
    VkBufferCreateInfo matrix_buffer_info = {};
    matrix_buffer_info.sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    matrix_buffer_info.pNext                 = nullptr;
    matrix_buffer_info.flags                 = {};
    matrix_buffer_info.size                  = sizeof(WarpMatrices);
    matrix_buffer_info.usage                 = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    matrix_buffer_info.sharingMode           = {};
    matrix_buffer_info.queueFamilyIndexCount = 0;
    matrix_buffer_info.pQueueFamilyIndices   = nullptr;

    VmaAllocationCreateInfo create_info = {};
    create_info.usage                   = VMA_MEMORY_USAGE_AUTO;
    create_info.flags         = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    create_info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    VK_ASSERT_SUCCESS(vmaCreateBuffer(vma_allocator_, &matrix_buffer_info, &create_info, &ow_matrices_uniform_buffer_,
                                      &ow_matrices_uniform_alloc_, &ow_matrices_uniform_alloc_info_))
    deletion_queue_.emplace([=]() {
        vmaDestroyBuffer(vma_allocator_, ow_matrices_uniform_buffer_, ow_matrices_uniform_alloc_);
    });
}

void openwarp_vk::create_descriptor_pool() {
    std::array<VkDescriptorPoolSize, 2> pool_sizes = {};
    pool_sizes[0].type                             = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    pool_sizes[0].descriptorCount                  = static_cast<uint32_t>(buffer_pool_->image_pool.size() * 2);
    pool_sizes[1].type                             = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_sizes[1].descriptorCount                  = static_cast<uint32_t>((2 * buffer_pool_->image_pool.size() + 1) * 2);

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.pNext         = nullptr;
    pool_info.flags         = 0;
    pool_info.maxSets       = 0;
    pool_info.poolSizeCount = 0;
    pool_info.pPoolSizes    = nullptr;
    pool_info.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
    pool_info.pPoolSizes    = pool_sizes.data();
    pool_info.maxSets       = static_cast<uint32_t>((buffer_pool_->image_pool.size() + 1) * 2);

    VK_ASSERT_SUCCESS(vkCreateDescriptorPool(display_provider_->vk_device_, &pool_info, nullptr, &descriptor_pool_))
}

void openwarp_vk::create_descriptor_sets() {
    for (int eye = 0; eye < 2; eye++) {
        // OpenWarp descriptor sets
        std::vector<VkDescriptorSetLayout> ow_layout = {buffer_pool_->image_pool.size(), ow_descriptor_set_layout_};
        VkDescriptorSetAllocateInfo        ow_alloc_info{};
        ow_alloc_info.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ow_alloc_info.pNext              = nullptr;
        ow_alloc_info.descriptorPool     = descriptor_pool_;
        ow_alloc_info.descriptorSetCount = 0;
        ow_alloc_info.pSetLayouts        = ow_layout.data();
        ow_alloc_info.descriptorSetCount = static_cast<uint32_t>(buffer_pool_->image_pool.size());

        ow_descriptor_sets_[eye].resize(buffer_pool_->image_pool.size());
        VK_ASSERT_SUCCESS(
            vkAllocateDescriptorSets(display_provider_->vk_device_, &ow_alloc_info, ow_descriptor_sets_[eye].data()))

        for (size_t image_idx = 0; image_idx < buffer_pool_->image_pool.size(); image_idx++) {
            VkDescriptorImageInfo image_info = {};
            image_info.sampler     = fb_sampler_;
            image_info.imageView   = buffer_pool_->image_pool[image_idx][eye].image_view;
            image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkDescriptorImageInfo depth_info = {};
            depth_info.sampler     = fb_sampler_;
            depth_info.imageView   = buffer_pool_->depth_image_pool[image_idx][eye].image_view;
            depth_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkDescriptorBufferInfo buffer_info = {};
            buffer_info.buffer = ow_matrices_uniform_buffer_;
            buffer_info.offset = 0;
            buffer_info.range = sizeof(WarpMatrices);

            VkWriteDescriptorSet image_set = {};
            image_set.sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            image_set.pNext            = nullptr;
            image_set.dstSet           = ow_descriptor_sets_[eye][image_idx];
            image_set.dstBinding       = 0;
            image_set.dstArrayElement  = 0;
            image_set.descriptorCount  = 1;
            image_set.descriptorType   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            image_set.pImageInfo       = &image_info;
            image_set.pBufferInfo      = nullptr;
            image_set.pTexelBufferView = nullptr;

            VkWriteDescriptorSet depth_set = {};
            depth_set.sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            depth_set.pNext            = nullptr;
            depth_set.dstSet           = ow_descriptor_sets_[eye][image_idx];
            depth_set.dstBinding       = 1;
            depth_set.dstArrayElement  = 0;
            depth_set.descriptorCount  = 1;
            depth_set.descriptorType   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            depth_set.pImageInfo       = &depth_info;
            depth_set.pBufferInfo      = nullptr;
            depth_set.pTexelBufferView = nullptr;

            VkWriteDescriptorSet buffer_set = {};
            buffer_set.sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            buffer_set.pNext            = nullptr;
            buffer_set.dstSet           = ow_descriptor_sets_[eye][image_idx];
            buffer_set.dstBinding       = 2;
            buffer_set.dstArrayElement  = 0;
            buffer_set.descriptorCount  = 1;
            buffer_set.descriptorType   = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            buffer_set.pImageInfo       = nullptr;
            buffer_set.pBufferInfo      = &buffer_info;
            buffer_set.pTexelBufferView = nullptr;

            std::array<VkWriteDescriptorSet, 3> ow_descriptor_writes = {image_set, depth_set, buffer_set};

            vkUpdateDescriptorSets(display_provider_->vk_device_, static_cast<uint32_t>(ow_descriptor_writes.size()),
                                   ow_descriptor_writes.data(), 0, nullptr);
        }

        // Distortion correction descriptor sets
        std::vector<VkDescriptorSetLayout> dc_layout     = {dp_descriptor_set_layout_};
        VkDescriptorSetAllocateInfo        dc_alloc_info = {};
        dc_alloc_info.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dc_alloc_info.pNext              = nullptr;
        dc_alloc_info.descriptorPool     = descriptor_pool_;
        dc_alloc_info.descriptorSetCount = 1;
        dc_alloc_info.pSetLayouts        = dc_layout.data();

        dp_descriptor_sets_[eye].resize(1);
        VK_ASSERT_SUCCESS(
            vkAllocateDescriptorSets(display_provider_->vk_device_, &dc_alloc_info, dp_descriptor_sets_[eye].data()))

        VkDescriptorImageInfo offscreen_image_info = {};
        offscreen_image_info.sampler     = fb_sampler_;
        offscreen_image_info.imageView   = offscreen_image_views_[eye];
        offscreen_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet dc_write_set = {};
        dc_write_set.sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        dc_write_set.pNext            = nullptr;
        dc_write_set.dstSet           = dp_descriptor_sets_[eye][0];
        dc_write_set.dstBinding       = 0;
        dc_write_set.dstArrayElement  = 0;
        dc_write_set.descriptorCount  = 1;
        dc_write_set.descriptorType   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        dc_write_set.pImageInfo       = &offscreen_image_info;
        dc_write_set.pBufferInfo      = nullptr;
        dc_write_set.pTexelBufferView = nullptr;

        std::array<VkWriteDescriptorSet, 1> dc_descriptor_writes = {dc_write_set};

        vkUpdateDescriptorSets(display_provider_->vk_device_, static_cast<uint32_t>(dc_descriptor_writes.size()),
                               dc_descriptor_writes.data(), 0, nullptr);
    }
}

void openwarp_vk::create_openwarp_pipeline() {
    // A renderpass also has to be created
    VkAttachmentDescription color_attachment{};
    color_attachment.flags         = 0;
    color_attachment.format        = VK_FORMAT_R8G8B8A8_UNORM; // this should match the offscreen image
    color_attachment.samples       = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp        = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp       = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference color_attachment_ref{};
    color_attachment_ref.attachment = 0;
    color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription depth_attachment{};
    depth_attachment.flags          = 0;
    depth_attachment.format         = VK_FORMAT_D16_UNORM; // this should match the offscreen image
    depth_attachment.samples        = VK_SAMPLE_COUNT_1_BIT;
    depth_attachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_attachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    depth_attachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    depth_attachment.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depth_attachment_ref{};
    depth_attachment_ref.attachment = 1;
    depth_attachment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.flags                   = 0;
    subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.inputAttachmentCount    = 0;
    subpass.pInputAttachments       = nullptr;
    subpass.colorAttachmentCount    = 1;
    subpass.pColorAttachments       = &color_attachment_ref;
    subpass.pResolveAttachments     = nullptr;
    subpass.pDepthStencilAttachment = &depth_attachment_ref;
    subpass.preserveAttachmentCount = 0;
    subpass.pPreserveAttachments    = nullptr;

    std::array<VkAttachmentDescription, 2> all_attachments = {color_attachment, depth_attachment};

    VkSubpassDependency dependency{};
    dependency.srcSubpass      = 0;
    dependency.dstSubpass      = VK_SUBPASS_EXTERNAL;
    dependency.srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask    = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependency.srcAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependency.dstAccessMask   = VK_ACCESS_SHADER_READ_BIT;
    dependency.dependencyFlags = 0;

    VkRenderPassCreateInfo render_pass_info{};
    render_pass_info.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.pNext           = nullptr;
    render_pass_info.flags           = 0;
    render_pass_info.attachmentCount = static_cast<uint32_t>(all_attachments.size());
    render_pass_info.pAttachments    = all_attachments.data();
    render_pass_info.subpassCount    = 1;
    render_pass_info.pSubpasses      = &subpass;
    render_pass_info.dependencyCount = 1;
    render_pass_info.pDependencies   = &dependency;

    VK_ASSERT_SUCCESS(vkCreateRenderPass(display_provider_->vk_device_, &render_pass_info, nullptr, &openwarp_render_pass_))

    if (openwarp_pipeline_ != VK_NULL_HANDLE) {
        throw std::runtime_error("openwarp_vk::create_pipeline: pipeline already created");
    }

    VkDevice device = display_provider_->vk_device_;

    auto           folder = std::string(SHADER_FOLDER);
    VkShaderModule vert   = vulkan::create_shader_module(device, vulkan::read_file(folder + "/openwarp_mesh.vert.spv"));
    VkShaderModule frag   = vulkan::create_shader_module(device, vulkan::read_file(folder + "/openwarp_mesh.frag.spv"));

    VkPipelineShaderStageCreateInfo vert_stage_info  = {};
    vert_stage_info.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vert_stage_info.pNext  = nullptr;
    vert_stage_info.flags  = {};
    vert_stage_info.stage  = VK_SHADER_STAGE_VERTEX_BIT;
    vert_stage_info.module = vert;
    vert_stage_info.pName  = "main";
    vert_stage_info.pSpecializationInfo = nullptr;

    VkPipelineShaderStageCreateInfo frage_stage_info = {};
    frage_stage_info.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    frage_stage_info.pNext  = nullptr;
    frage_stage_info.flags  = {};
    frage_stage_info.stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    frage_stage_info.module = frag;
    frage_stage_info.pName  = "main";
    frage_stage_info.pSpecializationInfo = nullptr;

    VkPipelineShaderStageCreateInfo shader_stages[] = {vert_stage_info, frage_stage_info};

    auto binding_description    = OpenWarpVertex::get_binding_description();
    auto attribute_descriptions = OpenWarpVertex::get_attribute_descriptions();

    VkPipelineVertexInputStateCreateInfo vertex_input_info = {};
    vertex_input_info.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_info.pNext                           = nullptr;
    vertex_input_info.flags                           = {};
    vertex_input_info.vertexBindingDescriptionCount   = 1;
    vertex_input_info.pVertexBindingDescriptions      = &binding_description;
    vertex_input_info.vertexAttributeDescriptionCount = static_cast<uint32_t>(attribute_descriptions.size());
    vertex_input_info.pVertexAttributeDescriptions    = attribute_descriptions.data();

    VkPipelineInputAssemblyStateCreateInfo input_assembly = {};
    input_assembly.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.pNext                  = nullptr;
    input_assembly.flags                  = {};
    input_assembly.topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly.primitiveRestartEnable = {};

    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.pNext                   = nullptr;
    rasterizer.flags                   = {};
    rasterizer.depthClampEnable        = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode             = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode                = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable         = VK_FALSE;
    rasterizer.depthBiasConstantFactor = 0.f;
    rasterizer.depthBiasClamp          = 0.f;
    rasterizer.depthBiasSlopeFactor    = 0.f;
    rasterizer.lineWidth               = 1.0f;

    // disable multisampling
    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.pNext                 = nullptr;
    multisampling.flags                 = {};
    multisampling.rasterizationSamples  = VK_SAMPLE_COUNT_1_BIT;
    multisampling.sampleShadingEnable   = VK_FALSE;
    multisampling.minSampleShading      = 0;
    multisampling.pSampleMask           = nullptr;
    multisampling.alphaToCoverageEnable = 0;
    multisampling.alphaToOneEnable      = 0;

    VkPipelineColorBlendAttachmentState color_blend_attachment = {};
    color_blend_attachment.blendEnable         = VK_FALSE;
    color_blend_attachment.srcColorBlendFactor = {};
    color_blend_attachment.dstColorBlendFactor = {};
    color_blend_attachment.colorBlendOp        = {};
    color_blend_attachment.srcAlphaBlendFactor = {};
    color_blend_attachment.dstAlphaBlendFactor = {};
    color_blend_attachment.alphaBlendOp        = {};
    color_blend_attachment.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT |
                                                 VK_COLOR_COMPONENT_G_BIT |
                                                 VK_COLOR_COMPONENT_B_BIT |
                                                 VK_COLOR_COMPONENT_A_BIT;

    // disable blending
    VkPipelineColorBlendStateCreateInfo color_blending = {};
    color_blending.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.pNext           = nullptr;
    color_blending.flags           = {};
    color_blending.logicOpEnable   = 0;
    color_blending.logicOp         = {};
    color_blending.attachmentCount = 1;
    color_blending.pAttachments    = &color_blend_attachment;
    //color_blending.blendConstants  = {};

    // enable depth testing
    VkPipelineDepthStencilStateCreateInfo depth_stencil = {};
    depth_stencil.sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil.pNext                 = nullptr;
    depth_stencil.flags                 = {};
    depth_stencil.depthTestEnable       = VK_TRUE;
    depth_stencil.depthWriteEnable      = VK_TRUE;
    depth_stencil.depthCompareOp        = rendering_params::reverse_z ? VK_COMPARE_OP_GREATER_OR_EQUAL : VK_COMPARE_OP_LESS_OR_EQUAL;
    depth_stencil.depthBoundsTestEnable = VK_FALSE;
    depth_stencil.stencilTestEnable     = VK_FALSE;
    depth_stencil.front                 = {};
    depth_stencil.back                  = {};
    depth_stencil.minDepthBounds        = 0.0f;
    depth_stencil.maxDepthBounds        = 1.0f;

    // use dynamic state instead of a fixed viewport
    std::vector<VkDynamicState> dynamic_states = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

    VkPipelineDynamicStateCreateInfo dynamic_state_create_info = {};
    dynamic_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state_create_info.pNext = nullptr;
    dynamic_state_create_info.flags = {};
    dynamic_state_create_info.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
    dynamic_state_create_info.pDynamicStates = dynamic_states.data();

    VkPipelineViewportStateCreateInfo viewport_state_create_info = {};
    viewport_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state_create_info.pNext         = nullptr;
    viewport_state_create_info.flags         = {};
    viewport_state_create_info.viewportCount = 1;
    viewport_state_create_info.pViewports    = nullptr;
    viewport_state_create_info.scissorCount  = 1;
    viewport_state_create_info.pScissors     = nullptr;

    VkPushConstantRange push_constant = {};
    push_constant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push_constant.offset = 0;
    push_constant.size = sizeof(uint32_t);

    VkPipelineLayoutCreateInfo pipeline_layout_info = {};
    pipeline_layout_info.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.pNext                  = nullptr;
    pipeline_layout_info.flags                  = {};
    pipeline_layout_info.setLayoutCount         = 1;
    pipeline_layout_info.pSetLayouts            = &ow_descriptor_set_layout_;
    pipeline_layout_info.pushConstantRangeCount = 1;
    pipeline_layout_info.pPushConstantRanges    = &push_constant;

    VK_ASSERT_SUCCESS(vkCreatePipelineLayout(device, &pipeline_layout_info, nullptr, &ow_pipeline_layout_))

    VkGraphicsPipelineCreateInfo pipeline_info = {};
    pipeline_info.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.pNext               = nullptr;
    pipeline_info.flags               = {};
    pipeline_info.stageCount          = 2;
    pipeline_info.pStages             = shader_stages;
    pipeline_info.pVertexInputState   = &vertex_input_info;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pTessellationState  = {};
    pipeline_info.pViewportState      = &viewport_state_create_info;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState   = &multisampling;
    pipeline_info.pDepthStencilState  = &depth_stencil;
    pipeline_info.pColorBlendState    = &color_blending;
    pipeline_info.pDynamicState       = &dynamic_state_create_info;
    pipeline_info.layout              = ow_pipeline_layout_;
    pipeline_info.renderPass          = openwarp_render_pass_;
    pipeline_info.subpass             = 0;
    pipeline_info.basePipelineHandle  = {};
    pipeline_info.basePipelineIndex   = 0;

    VK_ASSERT_SUCCESS(vkCreateGraphicsPipelines(display_provider_->vk_device_, VK_NULL_HANDLE, 1, &pipeline_info, nullptr,
                                                &openwarp_pipeline_))

    vkDestroyShaderModule(device, vert, nullptr);
    vkDestroyShaderModule(device, frag, nullptr);
}

VkPipeline openwarp_vk::create_distortion_correction_pipeline(VkRenderPass render_pass, [[maybe_unused]] uint32_t subpass) {
    if (pipeline_ != VK_NULL_HANDLE) {
        throw std::runtime_error("openwarp_vk::create_distortion_correction_pipeline: pipeline already created");
    }

    VkDevice device = display_provider_->vk_device_;

    auto           folder = std::string(SHADER_FOLDER);
    VkShaderModule vert   = vulkan::create_shader_module(device, vulkan::read_file(folder + "/distortion_correction.vert.spv"));
    VkShaderModule frag   = vulkan::create_shader_module(device, vulkan::read_file(folder + "/distortion_correction.frag.spv"));

    VkPipelineShaderStageCreateInfo vert_stage_info = {};
    vert_stage_info.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vert_stage_info.pNext  = nullptr;
    vert_stage_info.flags  = {};
    vert_stage_info.stage  = VK_SHADER_STAGE_VERTEX_BIT;
    vert_stage_info.module = vert;
    vert_stage_info.pName  = "main";
    vert_stage_info.pSpecializationInfo = nullptr;

    VkPipelineShaderStageCreateInfo frage_stage_info = {};
    frage_stage_info.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    frage_stage_info.pNext  = nullptr;
    frage_stage_info.flags  = {};
    frage_stage_info.stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    frage_stage_info.module = frag;
    frage_stage_info.pName  = "main";
    frage_stage_info.pSpecializationInfo = nullptr;

    VkPipelineShaderStageCreateInfo shader_stages[] = {vert_stage_info, frage_stage_info};

    auto binding_description    = DistortionCorrectionVertex::get_binding_description();
    auto attribute_descriptions = DistortionCorrectionVertex::get_attribute_descriptions();

    VkPipelineVertexInputStateCreateInfo vertex_input_info = {};
    vertex_input_info.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_info.pNext                           = nullptr;
    vertex_input_info.flags                           = {};
    vertex_input_info.vertexBindingDescriptionCount   = 1;
    vertex_input_info.pVertexBindingDescriptions      = &binding_description;
    vertex_input_info.vertexAttributeDescriptionCount = static_cast<uint32_t>(attribute_descriptions.size());
    vertex_input_info.pVertexAttributeDescriptions    = attribute_descriptions.data();

    VkPipelineInputAssemblyStateCreateInfo input_assembly = {};
    input_assembly.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.pNext                  = nullptr;
    input_assembly.flags                  = {};
    input_assembly.topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.pNext                   = nullptr;
    rasterizer.flags                   = {};
    rasterizer.depthClampEnable        = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode             = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode                = VK_CULL_MODE_NONE;
    rasterizer.frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable         = VK_FALSE;
    rasterizer.depthBiasConstantFactor = 0.f;
    rasterizer.depthBiasClamp          = 0.f;
    rasterizer.depthBiasSlopeFactor    = 0.f;
    rasterizer.lineWidth               = 1.0f;

    // disable multisampling
    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.pNext                 = nullptr;
    multisampling.flags                 = {};
    multisampling.rasterizationSamples  = VK_SAMPLE_COUNT_1_BIT;
    multisampling.sampleShadingEnable   = VK_FALSE;
    multisampling.minSampleShading      = 0.f;
    multisampling.pSampleMask           = {};
    multisampling.alphaToCoverageEnable = VK_FALSE;
    multisampling.alphaToOneEnable      = VK_FALSE;

    VkPipelineColorBlendAttachmentState color_blend_attachment = {};
    color_blend_attachment.blendEnable         = VK_FALSE;
    color_blend_attachment.srcColorBlendFactor = {};
    color_blend_attachment.dstColorBlendFactor = {};
    color_blend_attachment.colorBlendOp        = {};
    color_blend_attachment.srcAlphaBlendFactor = {};
    color_blend_attachment.dstAlphaBlendFactor = {};
    color_blend_attachment.alphaBlendOp        = {};
    color_blend_attachment.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT |
                                                 VK_COLOR_COMPONENT_G_BIT |
                                                 VK_COLOR_COMPONENT_B_BIT |
                                                 VK_COLOR_COMPONENT_A_BIT;

    // disable blending
    VkPipelineColorBlendStateCreateInfo color_blending = {};
    color_blending.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.pNext           = nullptr;
    color_blending.flags           = {};
    color_blending.logicOpEnable   = VK_FALSE;
    color_blending.logicOp         = {};
    color_blending.attachmentCount = 1;
    color_blending.pAttachments    = &color_blend_attachment;
    // color_blending.blendConstants  = {};

    // use dynamic state instead of a fixed viewport
    std::vector<VkDynamicState> dynamic_states = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

    VkPipelineDynamicStateCreateInfo dynamic_state_create_info = {};
    dynamic_state_create_info.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state_create_info.pNext             = nullptr;
    dynamic_state_create_info.flags             = {};
    dynamic_state_create_info.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
    dynamic_state_create_info.pDynamicStates    = dynamic_states.data();

    VkPipelineViewportStateCreateInfo viewport_state_create_info = {};
    viewport_state_create_info.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state_create_info.pNext         = nullptr;
    viewport_state_create_info.flags         = {};
    viewport_state_create_info.viewportCount = 1;
    viewport_state_create_info.pViewports    = nullptr;
    viewport_state_create_info.scissorCount  = 1;
    viewport_state_create_info.pScissors     = nullptr;

    VkPipelineLayoutCreateInfo pipeline_layout_info = {};
    pipeline_layout_info.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.pNext                  = nullptr;
    pipeline_layout_info.flags                  = {};
    pipeline_layout_info.setLayoutCount         = 1;
    pipeline_layout_info.pSetLayouts            = &dp_descriptor_set_layout_;
    pipeline_layout_info.pushConstantRangeCount = 0;
    pipeline_layout_info.pPushConstantRanges    = nullptr;

    VK_ASSERT_SUCCESS(vkCreatePipelineLayout(device, &pipeline_layout_info, nullptr, &dp_pipeline_layout_))

    VkGraphicsPipelineCreateInfo pipeline_info = {};
    pipeline_info.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.pNext               = nullptr;
    pipeline_info.flags               = {};
    pipeline_info.stageCount          = 2;
    pipeline_info.pStages             = shader_stages;
    pipeline_info.pVertexInputState   = &vertex_input_info;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pTessellationState  = {};
    pipeline_info.pViewportState      = &viewport_state_create_info;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState   = &multisampling;
    pipeline_info.pDepthStencilState  = nullptr;
    pipeline_info.pColorBlendState    = &color_blending;
    pipeline_info.pDynamicState       = &dynamic_state_create_info;
    pipeline_info.layout              = dp_pipeline_layout_;
    pipeline_info.renderPass          = render_pass;
    pipeline_info.subpass             = 0;
    pipeline_info.basePipelineHandle  = {};
    pipeline_info.basePipelineIndex   = 0;

    VK_ASSERT_SUCCESS(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline_))

    vkDestroyShaderModule(device, vert, nullptr);
    vkDestroyShaderModule(device, frag, nullptr);
    return pipeline_;
}

/* Compute a view matrix with rotation and position */
Eigen::Matrix4f openwarp_vk::create_camera_matrix(const pose::head_pose_type& pose, int eye) {
    Eigen::Matrix4f camera_matrix   = Eigen::Matrix4f::Identity();
    auto            ipd            = display_params::ipd / 2.0f;
    camera_matrix.block<3, 1>(0, 3) = pose.position + pose.orientation * Eigen::Vector3f(eye == 0 ? -ipd : ipd, 0, 0);
    camera_matrix.block<3, 3>(0, 0) = pose.orientation.toRotationMatrix();
    return camera_matrix;
}

Eigen::Matrix4f openwarp_vk::calculate_distortion_transform(const Eigen::Matrix4f& projection_matrix) {
    // Eigen stores matrices internally in column-major order.
    // However, the (i,j) accessors are row-major (i.e, the first argument
    // is which row, and the second argument is which column.)
    Eigen::Matrix4f tex_coord_projection;
    tex_coord_projection << 0.5f * projection_matrix(0, 0), 0.0f, 0.5f * projection_matrix(0, 2) - 0.5f, 0.0f, 0.0f,
        -0.5f * projection_matrix(1, 1), 0.5f * projection_matrix(1, 2) - 0.5f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        1.0f;

    return tex_coord_projection;
}
