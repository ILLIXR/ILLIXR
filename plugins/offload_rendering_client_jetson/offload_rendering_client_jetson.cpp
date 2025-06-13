#include "offload_rendering_client_jetson.hpp"

using namespace ILLIXR;
using namespace ILLIXR::data_format;

offload_rendering_client_jetson::offload_rendering_client_jetson(const std::string& name, phonebook* pb)
    : threadloop{name, pb}
    , switchboard_{pb->lookup_impl<switchboard>()}
    , log_{spdlogger("debug")}
    , display_provider_{pb->lookup_impl<vulkan::display_provider>()}
    , frames_reader_{switchboard_->get_buffered_reader<compressed_frame>("compressed_frames")}
    , pose_writer_{switchboard_->get_network_writer<fast_pose_type>("render_pose", {})}
    , pose_prediction_{pb->lookup_impl<pose_prediction>()}
    , clock_{pb->lookup_impl<relative_clock>()} {
    // Check environment variables for configuration
    use_depth_ = switchboard_->get_env_bool("ILLIXR_USE_DEPTH_IMAGES");
    if (use_depth_) {
        log_->debug("Encoding depth images for the client");
    } else {
        log_->debug("Not encoding depth images for the client");
    }
}

void offload_rendering_client_jetson::mmapi_init_decoders() {
    auto ret = color_decoder_.decoder_init();
    assert(ret == 0);
    if (use_depth_) {
        ret = depth_decoder_.decoder_init();
        assert(ret == 0);
    }
}

[[maybe_unused]] void offload_rendering_client_jetson::vk_resources_init() {
    command_pool_ =
        vulkan::create_command_pool(display_provider_->vk_device_, display_provider_->queues_[vulkan::queue::GRAPHICS].family);

    blit_color_cb_.resize(buffer_pool_->image_pool.size());
    blit_depth_cb_.resize(buffer_pool_->image_pool.size());

    // Initialize command buffers for each eye and frame
    for (size_t i = 0; i < buffer_pool_->image_pool.size(); i++) {
        for (auto eye = 0; eye < 2; eye++) {
            VkCommandBufferAllocateInfo allocInfo{};
            allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocInfo.commandPool        = command_pool_;
            allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandBufferCount = 1;

            VK_ASSERT_SUCCESS(vkAllocateCommandBuffers(display_provider_->vk_device_, &allocInfo, &blit_color_cb_[i][eye]));
            if (use_depth_) {
                VK_ASSERT_SUCCESS(vkAllocateCommandBuffers(display_provider_->vk_device_, &allocInfo, &blit_depth_cb_[i][eye]));
            }
        }
    }
}

void offload_rendering_client_jetson::mmapi_import_dmabuf(vulkan::vk_image& eye) {
    // Setup NVIDIA buffer surface parameters
    NvBufSurfaceMapParams params{};
    params.num_planes                  = 1;
    params.gpuId                       = 0;
    params.fd                          = eye.fd;
    params.totalSize                   = eye.allocation_info.size;
    params.memType                     = NVBUF_MEM_SURFACE_ARRAY;
    params.layout                      = NVBUF_LAYOUT_BLOCK_LINEAR;
    params.scanformat                  = NVBUF_DISPLAYSCANFORMAT_PROGRESSIVE;
    params.colorFormat                 = NVBUF_COLOR_FORMAT_BGRA;
    params.planes[0].width             = eye.image_info.extent.width;
    params.planes[0].height            = eye.image_info.extent.height;
    params.planes[0].pitch             = eye.image_info.extent.width * 4;
    params.planes[0].offset            = 0;
    params.planes[0].psize             = eye.allocation_info.size;
    params.planes[0].secondfieldoffset = 0;
    params.planes[0].blockheightlog2   = 4;

    NvBufSurface* surface;
    auto          ret  = NvBufSurfaceImport(&surface, &params);
    surface->numFilled = 1;
    assert(ret == 0);
}

void offload_rendering_client_jetson::setup(VkRenderPass render_pass, uint32_t subpass,
                                            std::shared_ptr<vulkan::buffer_pool<fast_pose_type>> buffer_pool) {
    (void) render_pass;
    (void) subpass;
    this->buffer_pool_ = buffer_pool;
    vk_resources_init();

    // Import DMA buffers for all images
    for (auto& image : buffer_pool_->image_pool) {
        for (auto& eye : image) {
            mmapi_import_dmabuf(eye);
        }
    }

    if (use_depth_) {
        for (auto& image : buffer_pool_->depth_image_pool) {
            for (auto& eye : image) {
                mmapi_import_dmabuf(eye);
            }
        }
    }

    ready_ = true;

    // Initialize image layouts
    for (auto& frame : buffer_pool_->image_pool) {
        for (auto& eye : frame) {
            auto cmd_buf = vulkan::begin_one_time_command(display_provider_->vk_device_, command_pool_);
            transition_layout(cmd_buf, eye.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            vulkan::end_one_time_command(display_provider_->vk_device_, command_pool_,
                                         display_provider_->queues_[vulkan::queue::GRAPHICS], cmd_buf);
        }
    }

    if (use_depth_) {
        for (auto& frame : buffer_pool_->depth_image_pool) {
            for (auto& eye : frame) {
                auto cmd_buf = vulkan::begin_one_time_command(display_provider_->vk_device_, command_pool_);
                transition_layout(cmd_buf, eye.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                vulkan::end_one_time_command(display_provider_->vk_device_, command_pool_,
                                             display_provider_->queues_[vulkan::queue::GRAPHICS], cmd_buf);
            }
        }
    }

    layout_transition_start_cmd_bufs_.resize(buffer_pool_->image_pool.size());
    layout_transition_end_cmd_bufs_.resize(buffer_pool_->image_pool.size());

    for (size_t i = 0; i < buffer_pool_->image_pool.size(); i++) {
        for (auto eye = 0; eye < 2; eye++) {
            layout_transition_start_cmd_bufs_[i][eye] =
                vulkan::create_command_buffer(display_provider_->vk_device_, command_pool_);
            VkCommandBufferBeginInfo begin_info{};
            begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin_info.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
            vkBeginCommandBuffer(layout_transition_start_cmd_bufs_[i][eye], &begin_info);
            transition_layout(layout_transition_start_cmd_bufs_[i][eye], buffer_pool_->image_pool[i][eye].image,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            if (use_depth_) {
                transition_layout(layout_transition_start_cmd_bufs_[i][eye], buffer_pool_->depth_image_pool[i][eye].image,
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            }
            vkEndCommandBuffer(layout_transition_start_cmd_bufs_[i][eye]);

            layout_transition_end_cmd_bufs_[i][eye] =
                vulkan::create_command_buffer(display_provider_->vk_device_, command_pool_);
            vkBeginCommandBuffer(layout_transition_end_cmd_bufs_[i][eye], &begin_info);
            transition_layout(layout_transition_end_cmd_bufs_[i][eye], buffer_pool_->image_pool[i][eye].image,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            if (use_depth_) {
                transition_layout(layout_transition_end_cmd_bufs_[i][eye], buffer_pool_->depth_image_pool[i][eye].image,
                                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            }
            vkEndCommandBuffer(layout_transition_end_cmd_bufs_[i][eye]);
        }
    }

    VkFenceCreateInfo blitFence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };

    vkCreateFence(display_provider_->vk_device_, &blitFence_info, nullptr, &blit_fence_);
}

void offload_rendering_client_jetson::destroy() {
    color_decoder_.decoder_destroy();
    if (use_depth_) {
        depth_decoder_.decoder_destroy();
    }
}

void offload_rendering_client_jetson::_p_thread_setup() {
    mmapi_init_decoders();
    // open file "left.h264" for writing

    decode_q_thread = std::make_shared<std::thread>([&]() {
        // auto file = fopen("left.h264", "wb");
        // auto frame = received_frame_;
        while (running_.load()) {
            queue_bytestream();
        }
    });
}

void offload_rendering_client_jetson::transition_layout(VkCommandBuffer cmd_buf, VkImage image, VkImageLayout old_layout,
                                                        VkImageLayout new_layout) {
    VkImageMemoryBarrier barrier{};
    barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout           = old_layout;
    barrier.newLayout           = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image               = image;
    barrier.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkPipelineStageFlags src_stage;
    VkPipelineStageFlags dst_stage;

    // Configure barrier based on layout transition type
    if (old_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        src_stage             = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dst_stage             = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        src_stage             = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dst_stage             = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        src_stage             = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dst_stage             = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        src_stage             = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dst_stage             = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_GENERAL && new_layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        src_stage             = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        dst_stage             = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_GENERAL && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        src_stage             = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        dst_stage             = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_GENERAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        src_stage             = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dst_stage             = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_GENERAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        src_stage             = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dst_stage             = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        src_stage             = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dst_stage             = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else {
        throw std::invalid_argument("unsupported layout transition");
    }

    vkCmdPipelineBarrier(cmd_buf, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

[[maybe_unused]] void offload_rendering_client_jetson::blitTo(uint8_t ind, uint8_t eye, int fd, bool depth) {
    VkImage  vkImage = VK_NULL_HANDLE;
    uint32_t width   = buffer_pool_->image_pool[ind][eye].image_info.extent.width;
    uint32_t height  = buffer_pool_->image_pool[ind][eye].image_info.extent.height;
    auto     blitCB  = depth ? blit_depth_cb_[ind][eye] : blit_color_cb_[ind][eye];

    // Create and set up the Vulkan image for the decoded frame
    if (vkImage == VK_NULL_HANDLE) {
        VkExternalMemoryImageCreateInfo dmaBufExternalMemoryImageCreateInfo{};
        dmaBufExternalMemoryImageCreateInfo.sType       = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
        dmaBufExternalMemoryImageCreateInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

        VkImageCreateInfo dmaBufImageCreateInfo{};
        dmaBufImageCreateInfo.sType       = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        dmaBufImageCreateInfo.pNext       = &dmaBufExternalMemoryImageCreateInfo;
        dmaBufImageCreateInfo.imageType   = VK_IMAGE_TYPE_2D;
        dmaBufImageCreateInfo.format      = buffer_pool_->image_pool[ind][eye].image_info.format;
        dmaBufImageCreateInfo.extent      = {width, height, 1};
        dmaBufImageCreateInfo.mipLevels   = 1;
        dmaBufImageCreateInfo.arrayLayers = 1;
        dmaBufImageCreateInfo.samples     = VK_SAMPLE_COUNT_1_BIT;
        dmaBufImageCreateInfo.tiling      = buffer_pool_->image_pool[ind][eye].image_info.tiling;
        dmaBufImageCreateInfo.usage       = VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        dmaBufImageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VK_ASSERT_SUCCESS(vkCreateImage(display_provider_->vk_device_, &dmaBufImageCreateInfo, nullptr, &vkImage));

        // Import the DMA-buf memory
        const int            duppedFd = dup(fd);
        VkMemoryRequirements imageMemoryRequirements{};
        vkGetImageMemoryRequirements(display_provider_->vk_device_, vkImage, &imageMemoryRequirements);

        const MemoryTypeResult memoryTypeResult =
            findMemoryType(display_provider_->vk_physical_device_, imageMemoryRequirements.memoryTypeBits,
                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        VkMemoryDedicatedAllocateInfo dedicatedAllocateInfo{};
        dedicatedAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
        dedicatedAllocateInfo.image = vkImage;

        VkImportMemoryFdInfoKHR importFdInfo{};
        importFdInfo.sType      = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR;
        importFdInfo.pNext      = &dedicatedAllocateInfo;
        importFdInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
        importFdInfo.fd         = duppedFd;

        VkMemoryAllocateInfo memoryAllocateInfo{};
        memoryAllocateInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memoryAllocateInfo.pNext           = &importFdInfo;
        memoryAllocateInfo.allocationSize  = imageMemoryRequirements.size;
        memoryAllocateInfo.memoryTypeIndex = memoryTypeResult.typeIndex;

        VkDeviceMemory importedImageMemory;
        VK_ASSERT_SUCCESS(vkAllocateMemory(display_provider_->vk_device_, &memoryAllocateInfo, nullptr, &importedImageMemory));
        VK_ASSERT_SUCCESS(vkBindImageMemory(display_provider_->vk_device_, vkImage, importedImageMemory, 0));
    }

    // Record and submit the copy commands
    VK_ASSERT_SUCCESS(vkResetCommandBuffer(blitCB, 0));
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    VK_ASSERT_SUCCESS(vkBeginCommandBuffer(blitCB, &beginInfo));

    transition_layout(blitCB, vkImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    VkImageCopy region{};
    region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.srcSubresource.layerCount = 1;
    region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.dstSubresource.layerCount = 1;
    region.extent.width              = width;
    region.extent.height             = height;
    region.extent.depth              = 1;

    vkCmdCopyImage(blitCB, vkImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   depth ? buffer_pool_->depth_image_pool[ind][eye].image : buffer_pool_->image_pool[ind][eye].image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    vkEndCommandBuffer(blitCB);

    // Submit all command buffers in sequence
    std::vector<VkCommandBuffer> cmd_bufs = {layout_transition_start_cmd_bufs_[ind][eye], blitCB,
                                             layout_transition_end_cmd_bufs_[ind][eye]};

    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = static_cast<uint32_t>(cmd_bufs.size());
    submitInfo.pCommandBuffers    = cmd_bufs.data();

    vulkan::locked_queue_submit(display_provider_->queues_[vulkan::queue::GRAPHICS], 1, &submitInfo, blit_fence_);
    VK_ASSERT_SUCCESS(vkWaitForFences(display_provider_->vk_device_, 1, &blit_fence_, VK_TRUE, UINT64_MAX));
}

void offload_rendering_client_jetson::queue_bytestream() {
    push_pose();
    if (!network_receive()) {
        return;
    }
    auto frame = received_frame_;

    // Queue color and depth frames for decoding
    color_decoder_.queue_output_plane_buffer(frame->left_color_nalu, frame->left_color_nalu_size);
    color_decoder_.queue_output_plane_buffer(frame->right_color_nalu, frame->right_color_nalu_size);

    if (use_depth_) {
        depth_decoder_.queue_output_plane_buffer(frame->left_depth_nalu, frame->left_depth_nalu_size);
        depth_decoder_.queue_output_plane_buffer(frame->right_depth_nalu, frame->right_depth_nalu_size);
    }
}

void offload_rendering_client_jetson::_p_one_iteration() {
    if (!ready_) {
        return;
    }

    auto ind        = buffer_pool_->src_acquire_image();
    auto decode_end = std::chrono::high_resolution_clock::now();

    // Decode color and depth frames in parallel
    std::thread color = std::thread([&] {
        for (auto eye = 0; eye < 2; eye++) {
            auto ret = color_decoder_.dec_capture(buffer_pool_->image_pool[ind][eye].fd);
            while (ret == -EAGAIN) {
                ret = color_decoder_.dec_capture(buffer_pool_->image_pool[ind][eye].fd);
            }
            assert(ret == 0 || ret == -EAGAIN);
        }
    });

    if (use_depth_) {
        std::thread depth = std::thread([&] {
            for (auto eye = 0; eye < 2; eye++) {
                auto ret = depth_decoder_.dec_capture(buffer_pool_->depth_image_pool[ind][eye].fd);
                while (ret == -EAGAIN) {
                    ret = depth_decoder_.dec_capture(buffer_pool_->depth_image_pool[ind][eye].fd);
                }
                assert(ret == 0 || ret == -EAGAIN);
            }
        });
        depth.join();
    }
    color.join();

    auto           transfer_end = std::chrono::high_resolution_clock::now();
    fast_pose_type decoded_frame_pose;
    {
        std::lock_guard<std::mutex> lock(pose_queue_mutex_);
        decoded_frame_pose = pose_queue_.front();
        pose_queue_.pop();
    }
    buffer_pool_->src_release_image(ind, std::move(decoded_frame_pose));

    // Calculate and track latency metrics
    auto now = time_point{std::chrono::duration<long, std::nano>{std::chrono::high_resolution_clock::now().time_since_epoch()}};
    auto pipeline_latency = now - decoded_frame_pose.predict_target_time;

    metrics_["capture"] += std::chrono::duration_cast<std::chrono::microseconds>(transfer_end - decode_end).count();
    metrics_["pipeline"] += std::chrono::duration_cast<std::chrono::microseconds>(pipeline_latency).count();

    // Update FPS and metrics every second
    if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - fps_start_time_).count() >=
        1) {
        log_->info("Decoder FPS: {}", fps_counter_);
        fps_start_time_ = std::chrono::high_resolution_clock::now();

        for (auto& metric : metrics_) {
            auto fps = std::max(fps_counter_, (uint16_t) 0);
            log_->info("{}: {}", metric.first, metric.second / (double) (fps));
            metric.second = 0;
        }
        fps_counter_ = 0;
    } else {
        fps_counter_++;
    }
}

void offload_rendering_client_jetson::push_pose() {
    auto current_pose = pose_prediction_->get_fast_pose();

    auto now = time_point{std::chrono::duration<long, std::nano>{std::chrono::high_resolution_clock::now().time_since_epoch()}};
    current_pose.predict_target_time   = now;
    current_pose.predict_computed_time = now;
    pose_writer_.put(std::make_shared<fast_pose_type>(current_pose));
}

bool offload_rendering_client_jetson::network_receive() {
    received_frame_ = frames_reader_.dequeue();
    if (received_frame_ == nullptr) {
        return false;
    }

    uint64_t timestamp =
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch())
            .count();
    auto pose            = received_frame_->pose;
    long network_latency = (long) timestamp - (long) received_frame_->sent_time;
    metrics_["network"] += network_latency / 1000000;
    pose.predict_target_time = time_point{std::chrono::duration<long, std::nano>{timestamp}};
    {
        std::lock_guard<std::mutex> lock(pose_queue_mutex_);
        pose_queue_.push(pose);
    }
    return true;
}

void offload_rendering_client_jetson::stop() {
    exit(0);
    running_ = false;
    decode_q_thread->join();
    threadloop::stop();
}

offload_rendering_client_jetson::MemoryTypeResult
offload_rendering_client_jetson::findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter,
                                                VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memoryProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);

    MemoryTypeResult result;
    result.found = false;

    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i) {
        if ((typeFilter & (1 << i)) && (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            result.typeIndex = i;
            result.found     = true;
            break;
        }
    }
    return result;
}

[[maybe_unused]] void offload_rendering_client_jetson::submit_command_buffer(VkCommandBuffer vk_command_buffer) {
    VkSubmitInfo submitInfo{
        VK_STRUCTURE_TYPE_SUBMIT_INFO, // sType
        nullptr,                       // pNext
        0,                             // waitSemaphoreCount
        nullptr,                       // pWaitSemaphores
        nullptr,                       // pWaitDstStageMask
        1,                             // commandBufferCount
        &vk_command_buffer,            // pCommandBuffers
        0,                             // signalSemaphoreCount
        nullptr                        // pSignalSemaphores
    };
    vulkan::locked_queue_submit(display_provider_->queues_[vulkan::queue::GRAPHICS], 1, &submitInfo, nullptr);
}
