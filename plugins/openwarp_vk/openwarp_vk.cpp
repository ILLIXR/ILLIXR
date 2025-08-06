#include "openwarp_vk.hpp"

#include "illixr/math_util.hpp"

using namespace ILLIXR;
using namespace ILLIXR::data_format;

openwarp_vk::openwarp_vk(const phonebook* pb)
    : phonebook_{pb}
    , switchboard_{phonebook_->lookup_impl<switchboard>()}
    , relative_clock_{phonebook_->lookup_impl<relative_clock>()}
    , pose_prediction_{phonebook_->lookup_impl<pose_prediction>()}
    , frame_writer_{switchboard_->get_writer<data_format::frame_to_be_saved>("frames_to_be_saved")}
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

    this->offloaded_rendering_ = switchboard_->get_env_bool("ILLIXR_OFFLOADING_RENDERING");
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
                        std::shared_ptr<vulkan::buffer_pool<fast_pose_type>> buffer_pool, bool input_texture_external) {
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

void openwarp_vk::update_uniforms(const fast_pose_type& render_pose, bool left) {
    num_update_uniforms_calls_++;

    fast_pose_type latest_pose;
    if (switchboard_->get_env_bool("ILLIXR_COMPARE_IMAGES")) {
        // If we are comparing images, we use the fake render pose
        latest_pose = disable_warp_ ? render_pose : pose_prediction_->get_fake_warp_pose();
    } else {
        latest_pose = disable_warp_ ? render_pose : pose_prediction_->get_fast_pose();
    }

    for (int eye = 0; eye < 2; eye++) {
        Eigen::Matrix4f renderedCameraMatrix = create_camera_matrix(render_pose.pose, eye);
        Eigen::Matrix4f currentCameraMatrix  = create_camera_matrix(latest_pose.pose, eye);

        Eigen::Matrix4f warpVP =
            basic_projection_[eye] * currentCameraMatrix.inverse(); // inverse of camera matrix is view matrix

        auto* ow_ubo = (WarpMatrices*) ow_matrices_uniform_alloc_info_.pMappedData;
        memcpy(&ow_ubo->render_inv_projection[eye], inverse_projection_[eye].data(), sizeof(Eigen::Matrix4f));
        memcpy(&ow_ubo->render_inv_view[eye], renderedCameraMatrix.data(), sizeof(Eigen::Matrix4f));
        memcpy(&ow_ubo->warp_view_projection[eye], warpVP.data(), sizeof(Eigen::Matrix4f));
    }
    if (left) log_pose_to_csv(relative_clock_->now(), render_pose, latest_pose);
}

void openwarp_vk::save_frame(VkFence fence) {
    int index = last_buffer_ind_ == 0 ? 1 : 0; // Use the other eye's index for saving
    std::cout << "[openwarp] Saving frame for index: " << index << std::endl;
    VkImage srcImageLeft = buffer_pool_->image_pool[last_buffer_ind_][0].image;
    frame_writer_.put(std::make_shared<data_format::frame_to_be_saved>(srcImageLeft, static_cast<uint32_t>(swapchain_width_ / 2),
                                                                        static_cast<uint32_t>(swapchain_height_), frame_count_, true,
                                                                        fence, "rendered_frames", last_buffer_ind_));
    VkImage srcImageRight = buffer_pool_->image_pool[last_buffer_ind_][1].image;
    frame_writer_.put(std::make_shared<data_format::frame_to_be_saved>(srcImageRight, static_cast<uint32_t>(swapchain_width_ / 2),
                                                                        static_cast<uint32_t>(swapchain_height_), frame_count_, false,
                                                                        fence, "rendered_frames", last_buffer_ind_));
}

// void openwarp_vk::save_frame(VkFence fence) {
//     return;
// }

void openwarp_vk::record_command_buffer(VkCommandBuffer commandBuffer, VkFramebuffer framebuffer, int buffer_ind, bool left, VkFence fence) {
    num_record_calls_++;

    if (left)
        frame_count_++;

    // Log the rendered frame
    // First, get the associated VkImage
    // std::pair<ILLIXR::vulkan::image_index_t, fast_pose_type> res =
    //     buffer_pool_->post_processing_acquire_image(static_cast<signed char>(last_frame_ind_));
    // auto ind = res.first;
    // if (ind != -1) {
    //     VkImage srcImage = buffer_pool_->image_pool[buffer_ind][left ? 0 : 1].image;
    //     frame_writer_.put(std::make_shared<data_format::frame_to_be_saved>(srcImage, static_cast<uint32_t>(swapchain_width_ / 2),
    //                                                                         static_cast<uint32_t>(swapchain_height_), frame_count_, left,
    //                                                                         "rendered_frames"));
    //     spdlog::get("illixr")->info("[openwarp] Published one rendered frame to be saved, frame count: {}", frame_count_);
    // }
    // VkImage srcImage = buffer_pool_->image_pool[last_buffer_ind_][left ? 0 : 1].image;
    // frame_writer_.put(std::make_shared<data_format::frame_to_be_saved>(srcImage, static_cast<uint32_t>(swapchain_width_ / 2),
    //                                                                     static_cast<uint32_t>(swapchain_height_), frame_count_, left,
    //                                                                     fence, "rendered_frames"));
    last_buffer_ind_ = buffer_ind;

    VkDeviceSize offsets = 0;
    VkClearValue clear_colors[2];
    clear_colors[0].color              = {0.0f, 0.0f, 0.0f, 1.0f};
    clear_colors[1].depthStencil.depth = rendering_params::reverse_z ? 0.0 : 1.0;

    // First render OpenWarp offscreen for a distortion correction pass later
    VkRenderPassBeginInfo ow_render_pass_info{.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                                              .pNext           = nullptr,
                                              .renderPass      = openwarp_render_pass_,
                                              .framebuffer     = offscreen_framebuffers_[left ? 0 : 1],
                                              .renderArea      = {.offset = {.x = 0, .y = 0},
                                                                  .extent = {.width  = static_cast<uint32_t>(swapchain_width_ / 2),
                                                                             .height = static_cast<uint32_t>(swapchain_height_)}},
                                              .clearValueCount = 2,
                                              .pClearValues    = clear_colors};

    VkViewport ow_viewport{.x        = 0,
                           .y        = 0,
                           .width    = static_cast<float>(swapchain_width_) / 2.f,
                           .height   = static_cast<float>(swapchain_height_),
                           .minDepth = 0.0f,
                           .maxDepth = 1.0f};

    VkRect2D ow_scissor{
        .offset = {.x = 0, .y = 0},
        .extent = {.width = static_cast<uint32_t>(swapchain_width_ / 2), .height = static_cast<uint32_t>(swapchain_height_)}};

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

    VkRenderPassBeginInfo dc_render_pass_info{
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .pNext           = nullptr,
        .renderPass      = distortion_correction_render_pass_,
        .framebuffer     = framebuffer,
        .renderArea      = {.offset = {.x = left ? 0 : static_cast<int32_t>(swapchain_width_ / 2), .y = 0},
                            .extent = {.width  = static_cast<uint32_t>(swapchain_width_ / 2),
                                       .height = static_cast<uint32_t>(swapchain_height_)}},
        .clearValueCount = 1,
        .pClearValues    = &clear_color};

    VkViewport dc_viewport{.x        = left ? 0.f : static_cast<float>(swapchain_width_) / 2.f,
                           .y        = 0,
                           .width    = static_cast<float>(swapchain_width_) / 2.f,
                           .height   = static_cast<float>(swapchain_height_),
                           .minDepth = 0.0f,
                           .maxDepth = 1.0f};

    VkRect2D dc_scissor{
        .offset = {.x = left ? 0 : static_cast<int32_t>(swapchain_width_ / 2), .y = 0},
        .extent = {.width = static_cast<uint32_t>(swapchain_width_ / 2), .height = static_cast<uint32_t>(swapchain_height_)}};

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
        VkImageCreateInfo image_info{.sType                 = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                                     .pNext                 = nullptr,
                                     .flags                 = 0,
                                     .imageType             = VK_IMAGE_TYPE_2D,
                                     .format                = VK_FORMAT_R8G8B8A8_UNORM,
                                     .extent                = {.width  = static_cast<uint32_t>(swapchain_width_ / 2),
                                                               .height = static_cast<uint32_t>(swapchain_height_),
                                                               .depth  = 1},
                                     .mipLevels             = 1,
                                     .arrayLayers           = 1,
                                     .samples               = VK_SAMPLE_COUNT_1_BIT,
                                     .tiling                = VK_IMAGE_TILING_OPTIMAL,
                                     .usage                 = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                                     .sharingMode           = {},
                                     .queueFamilyIndexCount = 0,
                                     .pQueueFamilyIndices   = nullptr,
                                     .initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED};

        VmaAllocationCreateInfo create_info = {.flags          = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
                                               .usage          = VMA_MEMORY_USAGE_AUTO,
                                               .requiredFlags  = {},
                                               .preferredFlags = {},
                                               .memoryTypeBits = {},
                                               .pool           = {},
                                               .pUserData      = nullptr,
                                               .priority       = 1.0f};

        VK_ASSERT_SUCCESS(vmaCreateImage(vma_allocator_, &image_info, &create_info, &offscreen_images_[eye],
                                         &offscreen_image_allocs_[eye], nullptr));

        VkImageViewCreateInfo view_info = {.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                           .pNext            = nullptr,
                                           .flags            = {},
                                           .image            = offscreen_images_[eye],
                                           .viewType         = VK_IMAGE_VIEW_TYPE_2D,
                                           .format           = VK_FORMAT_R8G8B8A8_UNORM,
                                           .components       = {.r = VK_COMPONENT_SWIZZLE_IDENTITY,
                                                                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                                                                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                                                                .a = VK_COMPONENT_SWIZZLE_IDENTITY},
                                           .subresourceRange = {.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                                                                .baseMipLevel   = 0,
                                                                .levelCount     = 1,
                                                                .baseArrayLayer = 0,
                                                                .layerCount     = 1}};

        VK_ASSERT_SUCCESS(vkCreateImageView(display_provider_->vk_device_, &view_info, nullptr, &offscreen_image_views_[eye]));

        VkImageCreateInfo depth_image_info{
            .sType                 = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .pNext                 = nullptr,
            .flags                 = {},
            .imageType             = VK_IMAGE_TYPE_2D,
            .format                = VK_FORMAT_D32_SFLOAT,
            .extent                = {.width  = static_cast<uint32_t>(swapchain_width_ / 2),
                                      .height = static_cast<uint32_t>(swapchain_height_),
                                      .depth  = 1},
            .mipLevels             = 1,
            .arrayLayers           = 1,
            .samples               = VK_SAMPLE_COUNT_1_BIT,
            .tiling                = VK_IMAGE_TILING_OPTIMAL,
            .usage                 = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            .sharingMode           = {},
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices   = nullptr,
            .initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED,
        };

        VmaAllocationCreateInfo depth_create_info = {.flags          = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
                                                     .usage          = VMA_MEMORY_USAGE_AUTO,
                                                     .requiredFlags  = {},
                                                     .preferredFlags = {},
                                                     .memoryTypeBits = 0,
                                                     .pool           = {},
                                                     .pUserData      = nullptr,
                                                     .priority       = 1.0f};

        VK_ASSERT_SUCCESS(vmaCreateImage(vma_allocator_, &depth_image_info, &depth_create_info, &offscreen_depths_[eye],
                                         &offscreen_depth_allocs_[eye], nullptr));

        VkImageViewCreateInfo depth_view_info = {.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                                 .pNext            = nullptr,
                                                 .flags            = {},
                                                 .image            = offscreen_depths_[eye],
                                                 .viewType         = VK_IMAGE_VIEW_TYPE_2D,
                                                 .format           = VK_FORMAT_D32_SFLOAT,
                                                 .components       = {.r = VK_COMPONENT_SWIZZLE_IDENTITY,
                                                                      .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                                                                      .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                                                                      .a = VK_COMPONENT_SWIZZLE_IDENTITY},
                                                 .subresourceRange = {.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT,
                                                                      .baseMipLevel   = 0,
                                                                      .levelCount     = 1,
                                                                      .baseArrayLayer = 0,
                                                                      .layerCount     = 1}};

        VK_ASSERT_SUCCESS(
            vkCreateImageView(display_provider_->vk_device_, &depth_view_info, nullptr, &offscreen_depth_views_[eye]));

        VkImageView attachments[2] = {offscreen_image_views_[eye], offscreen_depth_views_[eye]};

        // Need a framebuffer to render to
        VkFramebufferCreateInfo framebuffer_info = {
            .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .pNext           = nullptr,
            .flags           = 0,
            .renderPass      = openwarp_render_pass_,
            .attachmentCount = 2,
            .pAttachments    = attachments,
            .width           = static_cast<uint32_t>(swapchain_width_ / 2),
            .height          = static_cast<uint32_t>(swapchain_height_),
            .layers          = 1,
        };

        VK_ASSERT_SUCCESS(
            vkCreateFramebuffer(display_provider_->vk_device_, &framebuffer_info, nullptr, &offscreen_framebuffers_[eye]));
    }
}

void openwarp_vk::create_vertex_buffers() {
    // OpenWarp Vertices
    VkBufferCreateInfo ow_staging_buffer_info = {.sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                                 .pNext                 = nullptr,
                                                 .flags                 = {},
                                                 .size                  = sizeof(OpenWarpVertex) * num_openwarp_vertices_,
                                                 .usage                 = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                                 .sharingMode           = {},
                                                 .queueFamilyIndexCount = 0,
                                                 .pQueueFamilyIndices   = nullptr};

    VmaAllocationCreateInfo ow_staging_alloc_info = {};
    ow_staging_alloc_info.usage                   = VMA_MEMORY_USAGE_AUTO;
    ow_staging_alloc_info.flags                   = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

    VkBuffer      ow_staging_buffer;
    VmaAllocation ow_staging_alloc;
    VK_ASSERT_SUCCESS(vmaCreateBuffer(vma_allocator_, &ow_staging_buffer_info, &ow_staging_alloc_info, &ow_staging_buffer,
                                      &ow_staging_alloc, nullptr))

    VkBufferCreateInfo ow_buffer_info = {.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                         .pNext       = nullptr,
                                         .flags       = {},
                                         .size        = sizeof(OpenWarpVertex) * num_openwarp_vertices_,
                                         .usage       = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                         .sharingMode = {},
                                         .queueFamilyIndexCount = 0,
                                         .pQueueFamilyIndices   = nullptr};

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
    VkBufferCreateInfo dc_staging_buffer_info = {.sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                                 .pNext                 = nullptr,
                                                 .flags                 = {},
                                                 .size                  = sizeof(OpenWarpVertex) * num_openwarp_vertices_,
                                                 .usage                 = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                                 .sharingMode           = {},
                                                 .queueFamilyIndexCount = 0,
                                                 .pQueueFamilyIndices   = nullptr};

    VmaAllocationCreateInfo dc_staging_alloc_info = {};
    dc_staging_alloc_info.usage                   = VMA_MEMORY_USAGE_AUTO;
    dc_staging_alloc_info.flags                   = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

    VkBuffer      dc_staging_buffer;
    VmaAllocation dc_staging_alloc;
    VK_ASSERT_SUCCESS(vmaCreateBuffer(vma_allocator_, &dc_staging_buffer_info, &dc_staging_alloc_info, &dc_staging_buffer,
                                      &dc_staging_alloc, nullptr))

    VkBufferCreateInfo dc_buffer_info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                         .pNext = nullptr,
                                         .flags = {},
                                         .size  = sizeof(DistortionCorrectionVertex) * num_distortion_vertices_ * HMD::NUM_EYES,
                                         .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                         .sharingMode           = {},
                                         .queueFamilyIndexCount = 0,
                                         .pQueueFamilyIndices   = nullptr};
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
    VkBufferCreateInfo ow_staging_buffer_info = {.sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                                 .pNext                 = nullptr,
                                                 .flags                 = {},
                                                 .size                  = sizeof(uint32_t) * num_openwarp_indices_,
                                                 .usage                 = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                                 .sharingMode           = {},
                                                 .queueFamilyIndexCount = 0,
                                                 .pQueueFamilyIndices   = nullptr};

    VmaAllocationCreateInfo ow_staging_alloc_info = {};
    ow_staging_alloc_info.usage                   = VMA_MEMORY_USAGE_AUTO;
    ow_staging_alloc_info.flags                   = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

    VkBuffer      ow_staging_buffer;
    VmaAllocation ow_staging_alloc;
    VK_ASSERT_SUCCESS(vmaCreateBuffer(vma_allocator_, &ow_staging_buffer_info, &ow_staging_alloc_info, &ow_staging_buffer,
                                      &ow_staging_alloc, nullptr))

    VkBufferCreateInfo ow_buffer_info = {.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                         .pNext       = nullptr,
                                         .flags       = {},
                                         .size        = sizeof(uint32_t) * num_openwarp_indices_,
                                         .usage       = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                         .sharingMode = {},
                                         .queueFamilyIndexCount = 0,
                                         .pQueueFamilyIndices   = nullptr};

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
    VkBufferCreateInfo dc_staging_buffer_info = {.sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                                 .pNext                 = nullptr,
                                                 .flags                 = {},
                                                 .size                  = sizeof(uint32_t) * num_distortion_indices_,
                                                 .usage                 = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                                 .sharingMode           = {},
                                                 .queueFamilyIndexCount = 0,
                                                 .pQueueFamilyIndices   = nullptr};

    VmaAllocationCreateInfo dc_staging_alloc_info = {};
    dc_staging_alloc_info.usage                   = VMA_MEMORY_USAGE_AUTO;
    dc_staging_alloc_info.flags                   = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

    VkBuffer      dc_staging_buffer;
    VmaAllocation dc_staging_alloc;
    VK_ASSERT_SUCCESS(vmaCreateBuffer(vma_allocator_, &dc_staging_buffer_info, &dc_staging_alloc_info, &dc_staging_buffer,
                                      &dc_staging_alloc, nullptr))

    VkBufferCreateInfo dc_buffer_info = {.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                         .pNext       = nullptr,
                                         .flags       = {},
                                         .size        = sizeof(uint32_t) * num_distortion_indices_,
                                         .usage       = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                         .sharingMode = {},
                                         .queueFamilyIndexCount = 0,
                                         .pQueueFamilyIndices   = nullptr};

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
        if (!using_godot_) {
            spdlog::get("illixr")->info("Using unreal projection");
            math_util::unreal_projection(&basic_projection_[eye], index_params::fov_left[eye], index_params::fov_right[eye],
                                         index_params::fov_up[eye], index_params::fov_down[eye]);
        } else {
            spdlog::get("illixr")->info("Using godot projection");
            math_util::godot_projection(&basic_projection_[eye], index_params::fov_left[eye], index_params::fov_right[eye],
                                        index_params::fov_up[eye], index_params::fov_down[eye]);
        }

        if (!offloaded_rendering_) {
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
            Eigen::Matrix4f server_fov;
            if (!using_godot_) {
                spdlog::get("illixr")->info("Using unreal projection for offload rendering");
                math_util::unreal_projection(&server_fov, fov_left, fov_right, fov_up, fov_down);
            } else {
                spdlog::get("illixr")->info("Using godot projection for offload rendering");
                math_util::godot_projection(&server_fov, fov_left, fov_right, fov_up, fov_down);
            }

            inverse_projection_[eye] = server_fov.inverse();
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
    num_openwarp_indices_  = 2 * 3 * width * height;
    num_openwarp_vertices_ = (width + 1) * (height + 1);

    // Size the vectors accordingly
    openwarp_indices_.resize(num_openwarp_indices_);
    openwarp_vertices_.resize(num_openwarp_vertices_);

    // Build indices.
    for (size_t y = 0; y < height; y++) {
        for (size_t x = 0; x < width; x++) {
            const size_t offset = (y * width + x) * 6;
            // Each face is made of two triangles, so we need 6 indices per face.
            // Indices 0 to 2 form the first triangle at top left, and indices 3 to 5 form the second triangle at bottom right.
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

            // not sure why mapping the boarders outside the texture
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
    VkSamplerCreateInfo sampler_info = {.sType     = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                                        .pNext     = nullptr,
                                        .flags     = {},
                                        .magFilter = VK_FILTER_LINEAR, // how to interpolate texels that are magnified on screen
                                        .minFilter = VK_FILTER_LINEAR,
                                        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,

                                        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
                                        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
                                        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,

                                        .mipLodBias       = 0.f,
                                        .anisotropyEnable = VK_FALSE,
                                        .maxAnisotropy    = 0.f,
                                        .compareEnable    = VK_FALSE,
                                        .compareOp        = VK_COMPARE_OP_ALWAYS,
                                        .minLod           = 0.f,
                                        .maxLod           = 0.f,
                                        .borderColor      = VK_BORDER_COLOR_INT_OPAQUE_BLACK, // black outside the texture
                                        .unnormalizedCoordinates = VK_FALSE};

    VK_ASSERT_SUCCESS(vkCreateSampler(display_provider_->vk_device_, &sampler_info, nullptr, &fb_sampler_))
    deletion_queue_.emplace([=]() {
        vkDestroySampler(display_provider_->vk_device_, fb_sampler_, nullptr);
    });
}

void openwarp_vk::create_descriptor_set_layouts() {
    // OpenWarp descriptor set
    VkDescriptorSetLayoutBinding image_layout_binding = {.binding            = 0,
                                                         .descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                         .descriptorCount    = 1,
                                                         .stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT,
                                                         .pImmutableSamplers = nullptr};

    VkDescriptorSetLayoutBinding depth_layout_binding = {.binding         = 1,
                                                         .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                         .descriptorCount = 1,
                                                         .stageFlags =
                                                             VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                                         // .stageFlags                   = VK_SHADER_STAGE_VERTEX_BIT;
                                                         .pImmutableSamplers = nullptr};

    VkDescriptorSetLayoutBinding matrix_ubo_layout_binding = {.binding            = 2,
                                                              .descriptorType     = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                                              .descriptorCount    = 1,
                                                              .stageFlags         = VK_SHADER_STAGE_VERTEX_BIT,
                                                              .pImmutableSamplers = nullptr};

    std::array<VkDescriptorSetLayoutBinding, 3> ow_bindings    = {image_layout_binding, depth_layout_binding,
                                                                  matrix_ubo_layout_binding};
    VkDescriptorSetLayoutCreateInfo             ow_layout_info = {
                    .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                    .pNext        = nullptr,
                    .flags        = {},
                    .bindingCount = static_cast<uint32_t>(ow_bindings.size()),
                    .pBindings    = ow_bindings.data() // array of VkDescriptorSetLayoutBinding structs
    };

    VK_ASSERT_SUCCESS(
        vkCreateDescriptorSetLayout(display_provider_->vk_device_, &ow_layout_info, nullptr, &ow_descriptor_set_layout_))
    deletion_queue_.emplace([=]() {
        vkDestroyDescriptorSetLayout(display_provider_->vk_device_, ow_descriptor_set_layout_, nullptr);
    });

    // Distortion correction descriptor set
    VkDescriptorSetLayoutBinding offscreen_image_layout_binding = {
        .binding            = 0, // binding number in the shader
        .descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount    = 1,
        .stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT, // shader stages that can access the descriptor
        .pImmutableSamplers = nullptr};

    std::array<VkDescriptorSetLayoutBinding, 1> dc_bindings    = {offscreen_image_layout_binding};
    VkDescriptorSetLayoutCreateInfo             dc_layout_info = {
                    .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                    .pNext        = nullptr,
                    .flags        = {},
                    .bindingCount = static_cast<uint32_t>(dc_bindings.size()),
                    .pBindings    = dc_bindings.data() // array of VkDescriptorSetLayoutBinding structs
    };
    VK_ASSERT_SUCCESS(
        vkCreateDescriptorSetLayout(display_provider_->vk_device_, &dc_layout_info, nullptr, &dp_descriptor_set_layout_))
    deletion_queue_.emplace([=]() {
        vkDestroyDescriptorSetLayout(display_provider_->vk_device_, dp_descriptor_set_layout_, nullptr);
    });
}

void openwarp_vk::create_uniform_buffers() {
    // Matrix data
    VkBufferCreateInfo matrix_buffer_info = {.sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                             .pNext                 = nullptr,
                                             .flags                 = {},
                                             .size                  = sizeof(WarpMatrices),
                                             .usage                 = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                             .sharingMode           = {},
                                             .queueFamilyIndexCount = 0,
                                             .pQueueFamilyIndices   = nullptr};

    VmaAllocationCreateInfo createInfo = {};
    createInfo.usage                   = VMA_MEMORY_USAGE_AUTO;
    createInfo.flags         = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    createInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    VK_ASSERT_SUCCESS(vmaCreateBuffer(vma_allocator_, &matrix_buffer_info, &createInfo, &ow_matrices_uniform_buffer_,
                                      &ow_matrices_uniform_alloc_, &ow_matrices_uniform_alloc_info_))
    deletion_queue_.emplace([=]() {
        vmaDestroyBuffer(vma_allocator_, ow_matrices_uniform_buffer_, ow_matrices_uniform_alloc_);
    });
}

void openwarp_vk::create_descriptor_pool() {
    std::array<VkDescriptorPoolSize, 2> poolSizes = {};
    poolSizes[0].type                             = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount                  = buffer_pool_->image_pool.size() * 2;
    poolSizes[1].type                             = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount                  = (2 * buffer_pool_->image_pool.size() + 1) * 2;

    VkDescriptorPoolCreateInfo poolInfo = {.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                                           .pNext         = nullptr,
                                           .flags         = 0,
                                           .maxSets       = 0,
                                           .poolSizeCount = 0,
                                           .pPoolSizes    = nullptr};
    poolInfo.poolSizeCount              = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes                 = poolSizes.data();
    poolInfo.maxSets                    = (buffer_pool_->image_pool.size() + 1) * 2;

    VK_ASSERT_SUCCESS(vkCreateDescriptorPool(display_provider_->vk_device_, &poolInfo, nullptr, &descriptor_pool_))
}

void openwarp_vk::create_descriptor_sets() {
    for (int eye = 0; eye < 2; eye++) {
        // OpenWarp descriptor sets
        std::vector<VkDescriptorSetLayout> ow_layout = {buffer_pool_->image_pool.size(), ow_descriptor_set_layout_};
        VkDescriptorSetAllocateInfo        ow_alloc_info{.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                                                         .pNext              = nullptr,
                                                         .descriptorPool     = descriptor_pool_,
                                                         .descriptorSetCount = 0,
                                                         .pSetLayouts        = ow_layout.data()};
        ow_alloc_info.descriptorSetCount = buffer_pool_->image_pool.size();

        ow_descriptor_sets_[eye].resize(buffer_pool_->image_pool.size());
        VK_ASSERT_SUCCESS(
            vkAllocateDescriptorSets(display_provider_->vk_device_, &ow_alloc_info, ow_descriptor_sets_[eye].data()))

        for (size_t image_idx = 0; image_idx < buffer_pool_->image_pool.size(); image_idx++) {
            VkDescriptorImageInfo image_info = {.sampler     = fb_sampler_,
                                                .imageView   = buffer_pool_->image_pool[image_idx][eye].image_view,
                                                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

            VkDescriptorImageInfo depth_info = {.sampler     = fb_sampler_,
                                                .imageView   = buffer_pool_->depth_image_pool[image_idx][eye].image_view,
                                                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

            VkDescriptorBufferInfo buffer_info = {
                .buffer = ow_matrices_uniform_buffer_, .offset = 0, .range = sizeof(WarpMatrices)};

            std::array<VkWriteDescriptorSet, 3> ow_descriptor_writes = {
                VkWriteDescriptorSet{.sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                     .pNext            = nullptr,
                                     .dstSet           = ow_descriptor_sets_[eye][image_idx],
                                     .dstBinding       = 0,
                                     .dstArrayElement  = 0,
                                     .descriptorCount  = 1,
                                     .descriptorType   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                     .pImageInfo       = &image_info,
                                     .pBufferInfo      = nullptr,
                                     .pTexelBufferView = nullptr},
                VkWriteDescriptorSet{.sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                     .pNext            = nullptr,
                                     .dstSet           = ow_descriptor_sets_[eye][image_idx],
                                     .dstBinding       = 1,
                                     .dstArrayElement  = 0,
                                     .descriptorCount  = 1,
                                     .descriptorType   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                     .pImageInfo       = &depth_info,
                                     .pBufferInfo      = nullptr,
                                     .pTexelBufferView = nullptr},
                VkWriteDescriptorSet{.sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                     .pNext            = nullptr,
                                     .dstSet           = ow_descriptor_sets_[eye][image_idx],
                                     .dstBinding       = 2,
                                     .dstArrayElement  = 0,
                                     .descriptorCount  = 1,
                                     .descriptorType   = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                     .pImageInfo       = nullptr,
                                     .pBufferInfo      = &buffer_info,
                                     .pTexelBufferView = nullptr}};

            vkUpdateDescriptorSets(display_provider_->vk_device_, static_cast<uint32_t>(ow_descriptor_writes.size()),
                                   ow_descriptor_writes.data(), 0, nullptr);
        }

        // Distortion correction descriptor sets
        std::vector<VkDescriptorSetLayout> dc_layout     = {dp_descriptor_set_layout_};
        VkDescriptorSetAllocateInfo        dc_alloc_info = {.sType          = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                                                            .pNext          = nullptr,
                                                            .descriptorPool = descriptor_pool_,
                                                            .descriptorSetCount = 1,
                                                            .pSetLayouts        = dc_layout.data()};

        dp_descriptor_sets_[eye].resize(1);
        VK_ASSERT_SUCCESS(
            vkAllocateDescriptorSets(display_provider_->vk_device_, &dc_alloc_info, dp_descriptor_sets_[eye].data()))

        VkDescriptorImageInfo offscreen_image_info = {.sampler     = fb_sampler_,
                                                      .imageView   = offscreen_image_views_[eye],
                                                      .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

        std::array<VkWriteDescriptorSet, 1> dc_descriptor_writes = {
            VkWriteDescriptorSet{.sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                 .pNext            = nullptr,
                                 .dstSet           = dp_descriptor_sets_[eye][0],
                                 .dstBinding       = 0,
                                 .dstArrayElement  = 0,
                                 .descriptorCount  = 1,
                                 .descriptorType   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                 .pImageInfo       = &offscreen_image_info,
                                 .pBufferInfo      = nullptr,
                                 .pTexelBufferView = nullptr}};

        vkUpdateDescriptorSets(display_provider_->vk_device_, static_cast<uint32_t>(dc_descriptor_writes.size()),
                               dc_descriptor_writes.data(), 0, nullptr);
    }
}

void openwarp_vk::create_openwarp_pipeline() {
    // A renderpass also has to be created
    VkAttachmentDescription color_attachment{.flags         = 0,
                                             .format        = VK_FORMAT_R8G8B8A8_UNORM, // this should match the offscreen image
                                             .samples       = VK_SAMPLE_COUNT_1_BIT,
                                             .loadOp        = VK_ATTACHMENT_LOAD_OP_CLEAR,
                                             .storeOp       = VK_ATTACHMENT_STORE_OP_STORE,
                                             .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                             .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                             .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
                                             .finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

    VkAttachmentReference color_attachment_ref{.attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

    VkAttachmentDescription depth_attachment{.flags          = 0,
                                             .format         = VK_FORMAT_D32_SFLOAT, // this should match the offscreen image
                                             .samples        = VK_SAMPLE_COUNT_1_BIT,
                                             .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
                                             .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
                                             .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                             .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                             .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
                                             .finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkAttachmentReference depth_attachment_ref{.attachment = 1, .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass{.flags                   = 0,
                                 .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
                                 .inputAttachmentCount    = 0,
                                 .pInputAttachments       = nullptr,
                                 .colorAttachmentCount    = 1,
                                 .pColorAttachments       = &color_attachment_ref,
                                 .pResolveAttachments     = nullptr,
                                 .pDepthStencilAttachment = &depth_attachment_ref,
                                 .preserveAttachmentCount = 0,
                                 .pPreserveAttachments    = nullptr};

    std::array<VkAttachmentDescription, 2> all_attachments = {color_attachment, depth_attachment};

    VkSubpassDependency dependency{.srcSubpass      = 0,
                                   .dstSubpass      = VK_SUBPASS_EXTERNAL,
                                   .srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                   .dstStageMask    = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                   .srcAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                   .dstAccessMask   = VK_ACCESS_SHADER_READ_BIT,
                                   .dependencyFlags = 0};

    VkRenderPassCreateInfo render_pass_info{.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
                                            .pNext           = nullptr,
                                            .flags           = 0,
                                            .attachmentCount = static_cast<uint32_t>(all_attachments.size()),
                                            .pAttachments    = all_attachments.data(),
                                            .subpassCount    = 1,
                                            .pSubpasses      = &subpass,
                                            .dependencyCount = 1,
                                            .pDependencies   = &dependency};

    VK_ASSERT_SUCCESS(vkCreateRenderPass(display_provider_->vk_device_, &render_pass_info, nullptr, &openwarp_render_pass_));

    if (openwarp_pipeline_ != VK_NULL_HANDLE) {
        throw std::runtime_error("openwarp_vk::create_pipeline: pipeline already created");
    }

    VkDevice device = display_provider_->vk_device_;

    auto           folder = std::string(SHADER_FOLDER);
    VkShaderModule vert   = vulkan::create_shader_module(device, vulkan::read_file(folder + "/openwarp_mesh.vert.spv"));
    VkShaderModule frag   = vulkan::create_shader_module(device, vulkan::read_file(folder + "/openwarp_mesh.frag.spv"));

    VkPipelineShaderStageCreateInfo vert_stage_info  = {.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                                        .pNext  = nullptr,
                                                        .flags  = {},
                                                        .stage  = VK_SHADER_STAGE_VERTEX_BIT,
                                                        .module = vert,
                                                        .pName  = "main",
                                                        .pSpecializationInfo = nullptr};
    VkPipelineShaderStageCreateInfo frage_stage_info = {.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                                        .pNext  = nullptr,
                                                        .flags  = {},
                                                        .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
                                                        .module = frag,
                                                        .pName  = "main",
                                                        .pSpecializationInfo = nullptr};

    VkPipelineShaderStageCreateInfo shader_stages[] = {vert_stage_info, frage_stage_info};

    // Tell the shader how to interpret the vertex data (positions, UVs, etc.)
    auto bindingDescription    = OpenWarpVertex::get_binding_description();
    auto attributeDescriptions = OpenWarpVertex::get_attribute_descriptions();

    VkPipelineVertexInputStateCreateInfo vertex_input_info = {
        .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .pNext                           = nullptr,
        .flags                           = {},
        .vertexBindingDescriptionCount   = 1,
        .pVertexBindingDescriptions      = &bindingDescription,
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size()),
        .pVertexAttributeDescriptions    = attributeDescriptions.data()};

    VkPipelineInputAssemblyStateCreateInfo input_assembly = {.sType =
                                                                 VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                                                             .pNext                  = nullptr,
                                                             .flags                  = {},
                                                             .topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                                                             .primitiveRestartEnable = {}};

    VkPipelineRasterizationStateCreateInfo rasterizer = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                                                         .pNext = nullptr,
                                                         .flags = {},
                                                         .depthClampEnable        = VK_FALSE,
                                                         .rasterizerDiscardEnable = VK_FALSE,
                                                         .polygonMode             = VK_POLYGON_MODE_FILL,
                                                         .cullMode                = VK_CULL_MODE_BACK_BIT,
                                                         .frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE,
                                                         .depthBiasEnable         = VK_FALSE,
                                                         .depthBiasConstantFactor = 0.f,
                                                         .depthBiasClamp          = 0.f,
                                                         .depthBiasSlopeFactor    = 0.f,
                                                         .lineWidth               = 1.0f};

    // disable multisampling
    VkPipelineMultisampleStateCreateInfo multisampling = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                                                          .pNext = nullptr,
                                                          .flags = {},
                                                          .rasterizationSamples  = VK_SAMPLE_COUNT_1_BIT,
                                                          .sampleShadingEnable   = VK_FALSE,
                                                          .minSampleShading      = 0,
                                                          .pSampleMask           = nullptr,
                                                          .alphaToCoverageEnable = 0,
                                                          .alphaToOneEnable      = 0};

    VkPipelineColorBlendAttachmentState color_blend_attachment = {.blendEnable         = VK_FALSE,
                                                                  .srcColorBlendFactor = {},
                                                                  .dstColorBlendFactor = {},
                                                                  .colorBlendOp        = {},
                                                                  .srcAlphaBlendFactor = {},
                                                                  .dstAlphaBlendFactor = {},
                                                                  .alphaBlendOp        = {},
                                                                  .colorWriteMask      = VK_COLOR_COMPONENT_R_BIT |
                                                                      VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                                                                      VK_COLOR_COMPONENT_A_BIT};

    // disable blending
    VkPipelineColorBlendStateCreateInfo color_blending = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                                                          .pNext = nullptr,
                                                          .flags = {},
                                                          .logicOpEnable   = 0,
                                                          .logicOp         = {},
                                                          .attachmentCount = 1,
                                                          .pAttachments    = &color_blend_attachment,
                                                          .blendConstants  = {}};

    // enable depth testing
    VkPipelineDepthStencilStateCreateInfo depth_stencil = {
        .sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .pNext                 = nullptr,
        .flags                 = {},
        .depthTestEnable       = VK_TRUE,
        .depthWriteEnable      = VK_TRUE,
        .depthCompareOp        = rendering_params::reverse_z ? VK_COMPARE_OP_GREATER_OR_EQUAL : VK_COMPARE_OP_LESS_OR_EQUAL,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable     = VK_FALSE,
        .front                 = {},
        .back                  = {},
        .minDepthBounds        = 0.0f,
        .maxDepthBounds        = 1.0f};

    // use dynamic state instead of a fixed viewport
    std::vector<VkDynamicState> dynamic_states = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

    VkPipelineDynamicStateCreateInfo dynamic_state_create_info = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                                                                  .pNext = nullptr,
                                                                  .flags = {},
                                                                  .dynamicStateCount =
                                                                      static_cast<uint32_t>(dynamic_states.size()),
                                                                  .pDynamicStates = dynamic_states.data()};

    VkPipelineViewportStateCreateInfo viewport_state_create_info = {.sType =
                                                                        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                                                                    .pNext         = nullptr,
                                                                    .flags         = {},
                                                                    .viewportCount = 1,
                                                                    .pViewports    = nullptr,
                                                                    .scissorCount  = 1,
                                                                    .pScissors     = nullptr};

    VkPushConstantRange push_constant = {.stageFlags = VK_SHADER_STAGE_VERTEX_BIT, .offset = 0, .size = sizeof(uint32_t)};

    VkPipelineLayoutCreateInfo pipeline_layout_info = {.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                                       .pNext                  = nullptr,
                                                       .flags                  = {},
                                                       .setLayoutCount         = 1,
                                                       .pSetLayouts            = &ow_descriptor_set_layout_,
                                                       .pushConstantRangeCount = 1,
                                                       .pPushConstantRanges    = &push_constant};

    VK_ASSERT_SUCCESS(vkCreatePipelineLayout(device, &pipeline_layout_info, nullptr, &ow_pipeline_layout_))

    VkGraphicsPipelineCreateInfo pipeline_info = {.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                                                  .pNext               = nullptr,
                                                  .flags               = {},
                                                  .stageCount          = 2,
                                                  .pStages             = shader_stages,
                                                  .pVertexInputState   = &vertex_input_info,
                                                  .pInputAssemblyState = &input_assembly,
                                                  .pTessellationState  = {},
                                                  .pViewportState      = &viewport_state_create_info,
                                                  .pRasterizationState = &rasterizer,
                                                  .pMultisampleState   = &multisampling,
                                                  .pDepthStencilState  = &depth_stencil,
                                                  .pColorBlendState    = &color_blending,
                                                  .pDynamicState       = &dynamic_state_create_info,
                                                  .layout              = ow_pipeline_layout_,
                                                  .renderPass          = openwarp_render_pass_,
                                                  .subpass             = 0,
                                                  .basePipelineHandle  = {},
                                                  .basePipelineIndex   = 0};
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

    VkPipelineShaderStageCreateInfo vert_stage_info = {.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                                       .pNext  = nullptr,
                                                       .flags  = {},
                                                       .stage  = VK_SHADER_STAGE_VERTEX_BIT,
                                                       .module = vert,
                                                       .pName  = "main",
                                                       .pSpecializationInfo = nullptr};

    VkPipelineShaderStageCreateInfo frage_stage_info = {.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                                        .pNext  = nullptr,
                                                        .flags  = {},
                                                        .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
                                                        .module = frag,
                                                        .pName  = "main",
                                                        .pSpecializationInfo = nullptr};

    VkPipelineShaderStageCreateInfo shader_stages[] = {vert_stage_info, frage_stage_info};

    auto bindingDescription    = DistortionCorrectionVertex::get_binding_description();
    auto attributeDescriptions = DistortionCorrectionVertex::get_attribute_descriptions();

    VkPipelineVertexInputStateCreateInfo vertex_input_info = {
        .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .pNext                           = nullptr,
        .flags                           = {},
        .vertexBindingDescriptionCount   = 1,
        .pVertexBindingDescriptions      = &bindingDescription,
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size()),
        .pVertexAttributeDescriptions    = attributeDescriptions.data()};

    VkPipelineInputAssemblyStateCreateInfo input_assembly = {.sType =
                                                                 VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                                                             .pNext                  = nullptr,
                                                             .flags                  = {},
                                                             .topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                                                             .primitiveRestartEnable = VK_FALSE};

    VkPipelineRasterizationStateCreateInfo rasterizer = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                                                         .pNext = nullptr,
                                                         .flags = {},
                                                         .depthClampEnable        = VK_FALSE,
                                                         .rasterizerDiscardEnable = VK_FALSE,
                                                         .polygonMode             = VK_POLYGON_MODE_FILL,
                                                         .cullMode                = VK_CULL_MODE_NONE,
                                                         .frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE,
                                                         .depthBiasEnable         = VK_FALSE,
                                                         .depthBiasConstantFactor = 0.f,
                                                         .depthBiasClamp          = 0.f,
                                                         .depthBiasSlopeFactor    = 0.f,
                                                         .lineWidth               = 1.0f};

    // disable multisampling
    VkPipelineMultisampleStateCreateInfo multisampling = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                                                          .pNext = nullptr,
                                                          .flags = {},
                                                          .rasterizationSamples  = VK_SAMPLE_COUNT_1_BIT,
                                                          .sampleShadingEnable   = VK_FALSE,
                                                          .minSampleShading      = 0.f,
                                                          .pSampleMask           = {},
                                                          .alphaToCoverageEnable = VK_FALSE,
                                                          .alphaToOneEnable      = VK_FALSE};

    VkPipelineColorBlendAttachmentState color_blend_attachment = {.blendEnable         = VK_FALSE,
                                                                  .srcColorBlendFactor = {},
                                                                  .dstColorBlendFactor = {},
                                                                  .colorBlendOp        = {},
                                                                  .srcAlphaBlendFactor = {},
                                                                  .dstAlphaBlendFactor = {},
                                                                  .alphaBlendOp        = {},
                                                                  .colorWriteMask      = VK_COLOR_COMPONENT_R_BIT |
                                                                      VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                                                                      VK_COLOR_COMPONENT_A_BIT};

    // disable blending
    VkPipelineColorBlendStateCreateInfo color_blending = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                                                          .pNext = nullptr,
                                                          .flags = {},
                                                          .logicOpEnable   = VK_FALSE,
                                                          .logicOp         = {},
                                                          .attachmentCount = 1,
                                                          .pAttachments    = &color_blend_attachment,
                                                          .blendConstants  = {}};

    // use dynamic state instead of a fixed viewport
    std::vector<VkDynamicState> dynamic_states = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

    VkPipelineDynamicStateCreateInfo dynamic_state_create_info = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                                                                  .pNext = nullptr,
                                                                  .flags = {},
                                                                  .dynamicStateCount =
                                                                      static_cast<uint32_t>(dynamic_states.size()),
                                                                  .pDynamicStates = dynamic_states.data()};

    VkPipelineViewportStateCreateInfo viewport_state_create_info = {.sType =
                                                                        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                                                                    .pNext         = nullptr,
                                                                    .flags         = {},
                                                                    .viewportCount = 1,
                                                                    .pViewports    = nullptr,
                                                                    .scissorCount  = 1,
                                                                    .pScissors     = nullptr};

    VkPipelineLayoutCreateInfo pipeline_layout_info = {.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                                       .pNext                  = nullptr,
                                                       .flags                  = {},
                                                       .setLayoutCount         = 1,
                                                       .pSetLayouts            = &dp_descriptor_set_layout_,
                                                       .pushConstantRangeCount = 0,
                                                       .pPushConstantRanges    = nullptr};

    VK_ASSERT_SUCCESS(vkCreatePipelineLayout(device, &pipeline_layout_info, nullptr, &dp_pipeline_layout_))

    VkGraphicsPipelineCreateInfo pipeline_info = {.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                                                  .pNext               = nullptr,
                                                  .flags               = {},
                                                  .stageCount          = 2,
                                                  .pStages             = shader_stages,
                                                  .pVertexInputState   = &vertex_input_info,
                                                  .pInputAssemblyState = &input_assembly,
                                                  .pTessellationState  = {},
                                                  .pViewportState      = &viewport_state_create_info,
                                                  .pRasterizationState = &rasterizer,
                                                  .pMultisampleState   = &multisampling,
                                                  .pDepthStencilState  = nullptr,
                                                  .pColorBlendState    = &color_blending,
                                                  .pDynamicState       = &dynamic_state_create_info,
                                                  .layout              = dp_pipeline_layout_,
                                                  .renderPass          = render_pass,
                                                  .subpass             = 0,
                                                  .basePipelineHandle  = {},
                                                  .basePipelineIndex   = 0};

    VK_ASSERT_SUCCESS(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline_))

    vkDestroyShaderModule(device, vert, nullptr);
    vkDestroyShaderModule(device, frag, nullptr);
    return pipeline_;
}

/* Compute a view matrix with rotation and position */
Eigen::Matrix4f openwarp_vk::create_camera_matrix(const pose_type& pose, int eye) {
    Eigen::Matrix4f cameraMatrix   = Eigen::Matrix4f::Identity();
    auto            ipd            = display_params::ipd / 2.0f;
    cameraMatrix.block<3, 1>(0, 3) = pose.position + pose.orientation * Eigen::Vector3f(eye == 0 ? -ipd : ipd, 0, 0);
    cameraMatrix.block<3, 3>(0, 0) = pose.orientation.toRotationMatrix();
    return cameraMatrix;
}

Eigen::Matrix4f openwarp_vk::calculate_distortion_transform(const Eigen::Matrix4f& projection_matrix) {
    // Eigen stores matrices internally in column-major order.
    // However, the (i,j) accessors are row-major (i.e, the first argument
    // is which row, and the second argument is which column.)
    Eigen::Matrix4f texCoordProjection;
    texCoordProjection << 0.5f * projection_matrix(0, 0), 0.0f, 0.5f * projection_matrix(0, 2) - 0.5f, 0.0f, 0.0f,
        -0.5f * projection_matrix(1, 1), 0.5f * projection_matrix(1, 2) - 0.5f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        1.0f;

    return texCoordProjection;
}
