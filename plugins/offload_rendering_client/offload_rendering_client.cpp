#include "offload_rendering_client.hpp"

#define OFFLOAD_RENDERING_FFMPEG_DECODER_NAME "hevc"

using namespace ILLIXR;
using namespace ILLIXR::data_format;
using namespace ILLIXR::vulkan::ffmpeg_utils;

offload_rendering_client::offload_rendering_client(const std::string& name, phonebook* pb)
    : threadloop{name, pb}
    , switchboard_{pb->lookup_impl<switchboard>()}
    , log_{spdlogger(nullptr)}
    , display_provider_{pb->lookup_impl<vulkan::display_provider>()}
    , frames_reader_{switchboard_->get_buffered_reader<compressed_frame>("compressed_frames")}
    , pose_writer_{switchboard_->get_network_writer<fast_pose_type>("render_pose", {})}
    , pose_prediction_{pb->lookup_impl<pose_prediction>()}
    , clock_{pb->lookup_impl<relative_clock>()} {
    display_provider_ffmpeg = display_provider_;

    // Configure depth frame handling
    use_depth_ = switchboard_->get_env_bool("ILLIXR_USE_DEPTH_IMAGES");
    log_->debug(use_depth_ ? "Encoding depth images for the client" : "Not encoding depth images for the client");
}

void offload_rendering_client::start() {
    ffmpeg_init_device();
    ffmpeg_init_cuda_device();
    threadloop::start();
}

void offload_rendering_client::setup(VkRenderPass render_pass, uint32_t subpass,
                                     std::shared_ptr<vulkan::buffer_pool<fast_pose_type>> buffer_pool) {
    (void) render_pass;
    (void) subpass;
    this->buffer_pool_ = buffer_pool;
    command_pool =
        vulkan::create_command_pool(display_provider_->vk_device_, display_provider_->queues_[vulkan::queue::GRAPHICS].family);

    // Initialize FFmpeg and frame resources
    ffmpeg_init_frame_ctx();
    ffmpeg_init_cuda_frame_ctx();
    ffmpeg_init_buffer_pool();
    ffmpeg_init_decoder();
    ready_ = true;

    // Initialize image layouts for color frames
    for (auto& frame : avvk_color_frames_) {
        for (auto& eye : frame) {
            auto cmd_buf = vulkan::begin_one_time_command(display_provider_->vk_device_, command_pool);
            transition_layout(cmd_buf, eye.frame, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            vulkan::end_one_time_command(display_provider_->vk_device_, command_pool,
                                         display_provider_->queues_[vulkan::queue::GRAPHICS], cmd_buf);
        }
    }

    // Initialize image layouts for depth frames if enabled
    if (use_depth_) {
        for (auto& frame : avvk_depth_frames_) {
            for (auto& eye : frame) {
                auto cmd_buf = vulkan::begin_one_time_command(display_provider_->vk_device_, command_pool);
                transition_layout(cmd_buf, eye.frame, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                vulkan::end_one_time_command(display_provider_->vk_device_, command_pool,
                                             display_provider_->queues_[vulkan::queue::GRAPHICS], cmd_buf);
            }
        }
    }

    // Create command buffers for layout transitions
    for (size_t i = 0; i < avvk_color_frames_.size(); i++) {
        for (auto eye = 0; eye < 2; eye++) {
            // Create start transition command buffers
            layout_transition_start_cmd_bufs_[i][eye] =
                vulkan::create_command_buffer(display_provider_->vk_device_, command_pool);
            VkCommandBufferBeginInfo begin_info{};
            begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin_info.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
            vkBeginCommandBuffer(layout_transition_start_cmd_bufs_[i][eye], &begin_info);
            transition_layout(layout_transition_start_cmd_bufs_[i][eye], avvk_color_frames_[i][eye].frame,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            if (use_depth_) {
                transition_layout(layout_transition_start_cmd_bufs_[i][eye], avvk_depth_frames_[i][eye].frame,
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            }
            vkEndCommandBuffer(layout_transition_start_cmd_bufs_[i][eye]);

            // Create end transition command buffers
            layout_transition_end_cmd_bufs_[i][eye] =
                vulkan::create_command_buffer(display_provider_->vk_device_, command_pool);
            vkBeginCommandBuffer(layout_transition_end_cmd_bufs_[i][eye], &begin_info);
            transition_layout(layout_transition_end_cmd_bufs_[i][eye], avvk_color_frames_[i][eye].frame,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            if (use_depth_) {
                transition_layout(layout_transition_end_cmd_bufs_[i][eye], avvk_depth_frames_[i][eye].frame,
                                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            }
            vkEndCommandBuffer(layout_transition_end_cmd_bufs_[i][eye]);
        }
    }

    // Create fence for synchronization
    VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };
    vkCreateFence(display_provider_->vk_device_, &fence_info, nullptr, &fence_);
}

void offload_rendering_client::destroy() {
    // Free color frame resources
    for (auto& frame : avvk_color_frames_) {
        for (auto& eye : frame) {
            av_frame_free(&eye.frame);
        }
    }

    // Free depth frame resources if enabled
    for (auto& frame : avvk_depth_frames_) {
        for (auto& eye : frame) {
            av_frame_free(&eye.frame);
        }
    }

    // Release FFmpeg contexts
    av_buffer_unref(&frame_ctx_);
    av_buffer_unref(&device_ctx_);
}

[[maybe_unused]] void offload_rendering_client::copy_image_to_cpu_and_save_file(AVFrame* frame) {
    auto cpu_av_frame    = av_frame_alloc();
    cpu_av_frame->format = AV_PIX_FMT_RGBA;
    auto ret             = av_hwframe_transfer_data(cpu_av_frame, frame, 0);
    AV_ASSERT_SUCCESS(ret);

    // save cpu_av_frame as png
    auto png_codec           = avcodec_find_encoder(AV_CODEC_ID_PNG);
    auto png_codec_ctx       = avcodec_alloc_context3(png_codec);
    png_codec_ctx->pix_fmt   = AV_PIX_FMT_RGBA;
    png_codec_ctx->width     = cpu_av_frame->width;
    png_codec_ctx->height    = cpu_av_frame->height;
    png_codec_ctx->time_base = {1, 60};
    png_codec_ctx->framerate = {60, 1};

    ret = avcodec_open2(png_codec_ctx, png_codec, nullptr);
    AV_ASSERT_SUCCESS(ret);
    AVPacket* png_packet = av_packet_alloc();
    ret                  = avcodec_send_frame(png_codec_ctx, cpu_av_frame);
    AV_ASSERT_SUCCESS(ret);
    ret = avcodec_receive_packet(png_codec_ctx, png_packet);
    AV_ASSERT_SUCCESS(ret);

    std::string filename = "frame_" + std::to_string(frame_count_) + ".png";
    FILE*       f        = fopen(filename.c_str(), "wb");
    fwrite(png_packet->data, 1, png_packet->size, f);
    fclose(f);

    av_packet_free(&png_packet);
    av_frame_free(&cpu_av_frame);
    avcodec_free_context(&png_codec_ctx);
}

[[maybe_unused]] void offload_rendering_client::save_nv12_img_to_png(AVFrame* cuda_frame) const {
    auto cpu_av_frame    = av_frame_alloc();
    cpu_av_frame->format = AV_PIX_FMT_NV12;
    auto ret             = av_hwframe_transfer_data(cpu_av_frame, cuda_frame, 0);
    AV_ASSERT_SUCCESS(ret);

    AVFrame* frameGRB = av_frame_alloc();
    frameGRB->width   = cpu_av_frame->width;
    frameGRB->height  = cpu_av_frame->height;
    frameGRB->format  = AV_PIX_FMT_RGBA;
    av_frame_get_buffer(frameGRB, 0);

    SwsContext* sws_context = sws_getContext(cpu_av_frame->width, cpu_av_frame->height, AV_PIX_FMT_NV12, frameGRB->width,
                                             frameGRB->height, AV_PIX_FMT_RGBA, SWS_BICUBIC, NULL, NULL, NULL);
    if (sws_context != NULL) {
        sws_scale(sws_context, cpu_av_frame->data, cpu_av_frame->linesize, 0, cpu_av_frame->height, frameGRB->data,
                  frameGRB->linesize);
    }

    // save cpu_av_frame as png
    auto png_codec           = avcodec_find_encoder(AV_CODEC_ID_PNG);
    auto png_codec_ctx       = avcodec_alloc_context3(png_codec);
    png_codec_ctx->pix_fmt   = AV_PIX_FMT_RGBA;
    png_codec_ctx->width     = cpu_av_frame->width;
    png_codec_ctx->height    = cpu_av_frame->height;
    png_codec_ctx->time_base = {1, 60};
    png_codec_ctx->framerate = {60, 1};

    ret = avcodec_open2(png_codec_ctx, png_codec, nullptr);
    AV_ASSERT_SUCCESS(ret);
    AVPacket* png_packet = av_packet_alloc();
    ret                  = avcodec_send_frame(png_codec_ctx, frameGRB);
    AV_ASSERT_SUCCESS(ret);
    ret = avcodec_receive_packet(png_codec_ctx, png_packet);
    AV_ASSERT_SUCCESS(ret);

    std::string filename = "frame_" + std::to_string(frame_count_) + ".png";
    FILE*       f        = fopen(filename.c_str(), "wb");
    fwrite(png_packet->data, 1, png_packet->size, f);
    fclose(f);

    av_packet_free(&png_packet);
    av_frame_free(&cpu_av_frame);
    avcodec_free_context(&png_codec_ctx);
}

// Vulkan layout transition
// supports: shader read <-> transfer dst
void offload_rendering_client::transition_layout(VkCommandBuffer cmd_buf, AVFrame* frame, VkImageLayout old_layout,
                                                 VkImageLayout new_layout) {
    auto vk_frame = reinterpret_cast<AVVkFrame*>(frame->data[0]);
    auto image    = vk_frame->img[0];

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
    } else {
        throw std::invalid_argument("unsupported layout transition");
    }

    vkCmdPipelineBarrier(cmd_buf, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void offload_rendering_client::_p_one_iteration() {
    if (!ready_) {
        return;
    }

    // Send latest pose to server
    push_pose();

    // Receive and process network data
    if (!network_receive()) {
        return;
    }

    // Track frame timing
    auto timestamp = std::chrono::high_resolution_clock::now();
    auto diff      = timestamp - decoded_frame_pose_.predict_target_time.time_since_epoch();

    // Decode frames
    auto decode_start = std::chrono::high_resolution_clock::now();
    for (auto eye = 0; eye < 2; eye++) {
        // Decode color frames
        auto ret = avcodec_send_packet(codec_color_ctx_, decode_src_color_packets_[eye]);
        if (ret == AVERROR(EAGAIN)) {
            throw std::runtime_error{"FFmpeg encoder returned EAGAIN. Internal buffer full? Try using a higher-end GPU."};
        }
        AV_ASSERT_SUCCESS(ret);

        // Decode depth frames if enabled
        if (use_depth_) {
            ret = avcodec_send_packet(codec_depth_ctx_, decode_src_depth_packets_[eye]);
            if (ret == AVERROR(EAGAIN)) {
                throw std::runtime_error{"FFmpeg encoder returned EAGAIN. Internal buffer full? Try using a higher-end GPU."};
            }
            AV_ASSERT_SUCCESS(ret);
        }
    }

    // Receive decoded frames
    for (auto eye = 0; eye < 2; eye++) {
        auto ret = avcodec_receive_frame(codec_color_ctx_, decode_out_color_frames_[eye]);
        assert(decode_out_color_frames_[eye]->format == AV_PIX_FMT_CUDA);
        AV_ASSERT_SUCCESS(ret);

        if (use_depth_) {
            ret = avcodec_receive_frame(codec_depth_ctx_, decode_out_depth_frames_[eye]);
            assert(decode_out_depth_frames_[eye]->format == AV_PIX_FMT_CUDA);
            AV_ASSERT_SUCCESS(ret);
        }
    }
    auto decode_end = std::chrono::high_resolution_clock::now();

    // Perform color space conversion
    for (auto eye = 0; eye < 2; eye++) {
        // Convert NV12 to YUV420
        NppiSize roi = {static_cast<int>(decode_out_color_frames_[eye]->width),
                        static_cast<int>(decode_out_color_frames_[eye]->height)};
        Npp8u*   pSrc[2];
        pSrc[0] = reinterpret_cast<Npp8u*>(decode_out_color_frames_[eye]->data[0]);
        pSrc[1] = reinterpret_cast<Npp8u*>(decode_out_color_frames_[eye]->data[1]);
        Npp8u* pDst[3];
        pDst[0] = yuv420_y_plane_;
        pDst[1] = yuv420_u_plane_;
        pDst[2] = yuv420_v_plane_;
        int dst_linesizes[3];
        dst_linesizes[0] = y_step_;
        dst_linesizes[1] = u_step_;
        dst_linesizes[2] = v_step_;

        auto ret = nppiNV12ToYUV420_8u_P2P3R(pSrc, decode_out_color_frames_[eye]->linesize[0], pDst, dst_linesizes, roi);
        assert(ret == NPP_SUCCESS);

        // Convert YUV420 to BGRA
        auto dst = reinterpret_cast<Npp8u*>(decode_converted_color_frames_[eye]->data[0]);
        ret      = nppiYUV420ToBGR_8u_P3C4R(pDst, dst_linesizes, dst, decode_converted_color_frames_[eye]->linesize[0], roi);
        assert(ret == NPP_SUCCESS);

        // Process depth frames if enabled
        if (use_depth_) {
            // NppiSize roi_depth = {static_cast<int>(decode_out_depth_frames_[eye]->width),
            //                       static_cast<int>(decode_out_depth_frames_[eye]->height)};
            Npp8u* pSrc_depth[2];
            pSrc_depth[0] = reinterpret_cast<Npp8u*>(decode_out_depth_frames_[eye]->data[0]);
            pSrc_depth[1] = reinterpret_cast<Npp8u*>(decode_out_depth_frames_[eye]->data[1]);
            ret = nppiNV12ToYUV420_8u_P2P3R(pSrc_depth, decode_out_depth_frames_[eye]->linesize[0], pDst, dst_linesizes, roi);
            assert(ret == NPP_SUCCESS);
            auto dst_depth = reinterpret_cast<Npp8u*>(decode_converted_depth_frames_[eye]->data[0]);
            ret =
                nppiYUV420ToBGR_8u_P3C4R(pDst, dst_linesizes, dst_depth, decode_converted_depth_frames_[eye]->linesize[0], roi);
            assert(ret == NPP_SUCCESS);
        }
        cudaDeviceSynchronize();
    }
    auto conversion_end = std::chrono::high_resolution_clock::now();

    // Update display buffers
    auto ind            = buffer_pool_->src_acquire_image();
    auto transfer_start = std::chrono::high_resolution_clock::now();

    auto* frames = reinterpret_cast<AVHWFramesContext*>(frame_ctx_->data);
    auto* vk     = static_cast<AVVulkanFramesContext*>(frames->hwctx);

    // Transfer frames to display buffers
    for (auto eye = 0; eye < 2; eye++) {
        vk->lock_frame(frames, avvk_color_frames_[ind][eye].vk_frame);
        if (use_depth_) {
            vk->lock_frame(frames, avvk_depth_frames_[ind][eye].vk_frame);
        }

        vkResetFences(display_provider_->vk_device_, 1, &fence_);

        // Set up synchronization
        std::vector<VkSemaphore>          timelines   = {avvk_color_frames_[ind][eye].vk_frame->sem[0]};
        std::vector<VkPipelineStageFlags> wait_stages = {VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT};
        if (use_depth_) {
            timelines.push_back(avvk_depth_frames_[ind][eye].vk_frame->sem[0]);
            wait_stages.push_back(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
        }

        std::vector<uint64_t> start_wait_values   = {avvk_color_frames_[ind][eye].vk_frame->sem_value[0]};
        std::vector<uint64_t> start_signal_values = {++avvk_color_frames_[ind][eye].vk_frame->sem_value[0]};
        if (use_depth_) {
            start_wait_values.push_back(avvk_depth_frames_[ind][eye].vk_frame->sem_value[0]);
            start_signal_values.push_back(++avvk_depth_frames_[ind][eye].vk_frame->sem_value[0]);
        }

        // Submit layout transition commands
        VkTimelineSemaphoreSubmitInfo transition_start_timeline = {
            .sType                     = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
            .pNext                     = nullptr,
            .waitSemaphoreValueCount   = static_cast<uint16_t>(start_wait_values.size()),
            .pWaitSemaphoreValues      = start_wait_values.data(),
            .signalSemaphoreValueCount = static_cast<uint16_t>(start_signal_values.size()),
            .pSignalSemaphoreValues    = start_signal_values.data(),
        };

        VkSubmitInfo transition_start_submit = {
            .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext                = &transition_start_timeline,
            .waitSemaphoreCount   = static_cast<uint16_t>(timelines.size()),
            .pWaitSemaphores      = timelines.data(),
            .pWaitDstStageMask    = wait_stages.data(),
            .commandBufferCount   = 1,
            .pCommandBuffers      = &layout_transition_start_cmd_bufs_[ind][eye],
            .signalSemaphoreCount = static_cast<uint16_t>(timelines.size()),
            .pSignalSemaphores    = timelines.data(),
        };
        vulkan::locked_queue_submit(display_provider_->queues_[vulkan::queue::GRAPHICS], 1, &transition_start_submit, nullptr);

        // Transfer frame data
        auto ret = av_hwframe_transfer_data(avvk_color_frames_[ind][eye].frame, decode_converted_color_frames_[eye], 0);
        AV_ASSERT_SUCCESS(ret);

        if (use_depth_) {
            ret = av_hwframe_transfer_data(avvk_depth_frames_[ind][eye].frame, decode_converted_depth_frames_[eye], 0);
            AV_ASSERT_SUCCESS(ret);
        }

        // Submit end transition commands
        std::vector<uint64_t> end_wait_values   = {avvk_color_frames_[ind][eye].vk_frame->sem_value[0]};
        std::vector<uint64_t> end_signal_values = {++avvk_color_frames_[ind][eye].vk_frame->sem_value[0]};
        if (use_depth_) {
            end_wait_values.push_back(avvk_depth_frames_[ind][eye].vk_frame->sem_value[0]);
            end_signal_values.push_back(++avvk_depth_frames_[ind][eye].vk_frame->sem_value[0]);
        }

        VkTimelineSemaphoreSubmitInfo transition_end_timeline = {
            .sType                     = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
            .pNext                     = nullptr,
            .waitSemaphoreValueCount   = static_cast<uint16_t>(end_wait_values.size()),
            .pWaitSemaphoreValues      = end_wait_values.data(),
            .signalSemaphoreValueCount = static_cast<uint16_t>(end_signal_values.size()),
            .pSignalSemaphoreValues    = end_signal_values.data(),
        };

        VkSubmitInfo transition_end_submit = {
            .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext                = &transition_end_timeline,
            .waitSemaphoreCount   = static_cast<uint16_t>(timelines.size()),
            .pWaitSemaphores      = timelines.data(),
            .pWaitDstStageMask    = wait_stages.data(),
            .commandBufferCount   = 1,
            .pCommandBuffers      = &layout_transition_end_cmd_bufs_[ind][eye],
            .signalSemaphoreCount = static_cast<uint16_t>(timelines.size()),
            .pSignalSemaphores    = timelines.data(),
        };
        vulkan::locked_queue_submit(display_provider_->queues_[vulkan::queue::GRAPHICS], 1, &transition_end_submit, fence_);
        vkWaitForFences(display_provider_->vk_device_, 1, &fence_, VK_TRUE, UINT64_MAX);

        // Update frame counters and release resources
        if (use_depth_) {
            decode_out_color_frames_[eye]->pts = static_cast<int64_t>(frame_count_++);
            decode_out_depth_frames_[eye]->pts = static_cast<int64_t>(frame_count_++);

            vulkan::wait_timeline_semaphores(
                display_provider_->vk_device_,
                {{avvk_color_frames_[ind][eye].vk_frame->sem[0], avvk_color_frames_[ind][eye].vk_frame->sem_value[0]},
                 {avvk_depth_frames_[ind][eye].vk_frame->sem[0], avvk_depth_frames_[ind][eye].vk_frame->sem_value[0]}});

            vk->unlock_frame(frames, avvk_color_frames_[ind][eye].vk_frame);
            vk->unlock_frame(frames, avvk_depth_frames_[ind][eye].vk_frame);
        } else {
            decode_out_color_frames_[eye]->pts = static_cast<int64_t>(frame_count_++);

            vulkan::wait_timeline_semaphores(
                display_provider_->vk_device_,
                {{avvk_color_frames_[ind][eye].vk_frame->sem[0], avvk_color_frames_[ind][eye].vk_frame->sem_value[0]}});

            vk->unlock_frame(frames, avvk_color_frames_[ind][eye].vk_frame);
        }
    }

    auto transfer_end = std::chrono::high_resolution_clock::now();
    buffer_pool_->src_release_image(ind, std::move(decoded_frame_pose_));

    // Update performance metrics
    metrics_["decode"] += std::chrono::duration_cast<std::chrono::microseconds>(decode_end - decode_start).count();
    metrics_["conversion"] += std::chrono::duration_cast<std::chrono::microseconds>(conversion_end - decode_end).count();
    metrics_["transfer"] += std::chrono::duration_cast<std::chrono::microseconds>(transfer_end - transfer_start).count();

    // Log performance metrics every second
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

void offload_rendering_client::push_pose() {
    auto current_pose = pose_prediction_->get_fast_pose();

    auto now = time_point{std::chrono::duration<long, std::nano>{std::chrono::high_resolution_clock::now().time_since_epoch()}};
    current_pose.predict_target_time   = now;
    current_pose.predict_computed_time = now;
    std::cout << "Pushing new pose" << std::endl;
    pose_writer_.put(std::make_shared<fast_pose_type>(current_pose));
}

bool offload_rendering_client::network_receive() {
    // Free previous packets if they exist
    if (decode_src_color_packets_[0] != nullptr) {
        av_packet_free_side_data(decode_src_color_packets_[0]);
        av_packet_free_side_data(decode_src_color_packets_[1]);
        av_packet_free(&decode_src_color_packets_[0]);
        av_packet_free(&decode_src_color_packets_[1]);
        if (use_depth_) {
            av_packet_free_side_data(decode_src_depth_packets_[0]);
            av_packet_free_side_data(decode_src_depth_packets_[1]);
            av_packet_free(&decode_src_depth_packets_[0]);
            av_packet_free(&decode_src_depth_packets_[1]);
        }
    }

    // Receive new frame data
    auto frame = frames_reader_.dequeue();
    if (frame == nullptr) {
        return false;
    }

    // Store frame data
    decode_src_color_packets_[0] = frame->left_color;
    decode_src_color_packets_[1] = frame->right_color;
    if (use_depth_) {
        decode_src_depth_packets_[0] = frame->left_depth;
        decode_src_depth_packets_[1] = frame->right_depth;
    }

    // Track frame timing
    uint64_t timestamp =
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch())
            .count();
    auto diff_ns = timestamp - frame->sent_time;
    log_->info("Network latency (ms): {}", static_cast<double>(diff_ns) / 1000000.0);

    decoded_frame_pose_ = frame->pose;
    return true;
}

[[maybe_unused]] void offload_rendering_client::submit_command_buffer(VkCommandBuffer vk_command_buffer) {
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

void offload_rendering_client::ffmpeg_init_device() {
    this->device_ctx_     = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_VULKAN);
    auto hwdev_ctx        = reinterpret_cast<AVHWDeviceContext*>(device_ctx_->data);
    auto vulkan_hwdev_ctx = reinterpret_cast<AVVulkanDeviceContext*>(hwdev_ctx->hwctx);

    // Configure Vulkan device context
    vulkan_hwdev_ctx->inst            = display_provider_->vk_instance_;
    vulkan_hwdev_ctx->phys_dev        = display_provider_->vk_physical_device_;
    vulkan_hwdev_ctx->act_dev         = display_provider_->vk_device_;
    vulkan_hwdev_ctx->device_features = display_provider_->features_;

    // Set up queue families
    for (auto& queue : display_provider_->queues_) {
        switch (queue.first) {
        case vulkan::queue::GRAPHICS:
            vulkan_hwdev_ctx->queue_family_index    = static_cast<int>(queue.second.family);
            vulkan_hwdev_ctx->nb_graphics_queues    = 1;
            vulkan_hwdev_ctx->queue_family_tx_index = static_cast<int>(queue.second.family);
            vulkan_hwdev_ctx->nb_tx_queues          = 1;
            break;
        case vulkan::queue::COMPUTE:
            vulkan_hwdev_ctx->queue_family_comp_index = static_cast<int>(queue.second.family);
            vulkan_hwdev_ctx->nb_comp_queues          = 1;
        default:
            break;
        }
    }

    // Configure dedicated transfer queue if available
    if (display_provider_->queues_.find(vulkan::queue::DEDICATED_TRANSFER) != display_provider_->queues_.end()) {
        vulkan_hwdev_ctx->queue_family_tx_index =
            static_cast<int>(display_provider_->queues_[vulkan::queue::DEDICATED_TRANSFER].family);
        vulkan_hwdev_ctx->nb_tx_queues = 1;
    }

    // Vulkan Video not used in this implementation
    vulkan_hwdev_ctx->nb_encode_queues          = 0;
    vulkan_hwdev_ctx->nb_decode_queues          = 0;
    vulkan_hwdev_ctx->queue_family_encode_index = -1;
    vulkan_hwdev_ctx->queue_family_decode_index = -1;

    // Set up extensions and callbacks
    vulkan_hwdev_ctx->alloc                      = nullptr;
    vulkan_hwdev_ctx->get_proc_addr              = vkGetInstanceProcAddr;
    vulkan_hwdev_ctx->enabled_inst_extensions    = display_provider_->enabled_instance_extensions_.data();
    vulkan_hwdev_ctx->nb_enabled_inst_extensions = static_cast<int>(display_provider_->enabled_instance_extensions_.size());
    vulkan_hwdev_ctx->enabled_dev_extensions     = display_provider_->enabled_device_extensions_.data();
    vulkan_hwdev_ctx->nb_enabled_dev_extensions  = static_cast<int>(display_provider_->enabled_device_extensions_.size());
    vulkan_hwdev_ctx->lock_queue                 = &ffmpeg_lock_queue;
    vulkan_hwdev_ctx->unlock_queue               = &ffmpeg_unlock_queue;

    AV_ASSERT_SUCCESS(av_hwdevice_ctx_init(device_ctx_));
    log_->info("FFmpeg Vulkan hwdevice context initialized");
}

void offload_rendering_client::ffmpeg_init_cuda_device() {
    auto ret = av_hwdevice_ctx_create(&cuda_device_ctx_, AV_HWDEVICE_TYPE_CUDA, nullptr, nullptr, 0);
    AV_ASSERT_SUCCESS(ret);
    if (cuda_device_ctx_ == nullptr) {
        throw std::runtime_error{"Failed to create FFmpeg CUDA hwdevice context"};
    }
    log_->info("FFmpeg CUDA hwdevice context initialized");
}

void offload_rendering_client::ffmpeg_init_frame_ctx() {
    assert(this->buffer_pool_ != nullptr);
    this->frame_ctx_ = av_hwframe_ctx_alloc(device_ctx_);
    if (!frame_ctx_) {
        throw std::runtime_error{"Failed to create FFmpeg Vulkan hwframe context"};
    }

    auto hwframe_ctx    = reinterpret_cast<AVHWFramesContext*>(frame_ctx_->data);
    hwframe_ctx->format = AV_PIX_FMT_VULKAN;
    auto pix_format     = vulkan::ffmpeg_utils::get_pix_format_from_vk_format(buffer_pool_->image_pool[0][0].image_info.format);
    if (!pix_format) {
        throw std::runtime_error{"Unsupported Vulkan image format when creating FFmpeg Vulkan hwframe context"};
    }
    assert(pix_format == AV_PIX_FMT_BGRA);
    hwframe_ctx->sw_format         = AV_PIX_FMT_BGRA;
    hwframe_ctx->width             = static_cast<int>(buffer_pool_->image_pool[0][0].image_info.extent.width);
    hwframe_ctx->height            = static_cast<int>(buffer_pool_->image_pool[0][0].image_info.extent.height);
    hwframe_ctx->initial_pool_size = 0;
    auto ret                       = av_hwframe_ctx_init(frame_ctx_);
    AV_ASSERT_SUCCESS(ret);
}

AVBufferRef* offload_rendering_client::create_cuda_frame_ctx(AVPixelFormat fmt) {
    auto cuda_frame_ref = av_hwframe_ctx_alloc(cuda_device_ctx_);
    if (!cuda_frame_ref) {
        throw std::runtime_error{"Failed to create FFmpeg CUDA hwframe context"};
    }
    auto cuda_hwframe_ctx               = reinterpret_cast<AVHWFramesContext*>(cuda_frame_ref->data);
    cuda_hwframe_ctx->format            = AV_PIX_FMT_CUDA;
    cuda_hwframe_ctx->sw_format         = fmt;
    cuda_hwframe_ctx->width             = static_cast<int>(buffer_pool_->image_pool[0][0].image_info.extent.width);
    cuda_hwframe_ctx->height            = static_cast<int>(buffer_pool_->image_pool[0][0].image_info.extent.height);
    cuda_hwframe_ctx->initial_pool_size = 0;
    auto ret                            = av_hwframe_ctx_init(cuda_frame_ref);
    AV_ASSERT_SUCCESS(ret);
    return cuda_frame_ref;
}

void offload_rendering_client::ffmpeg_init_cuda_frame_ctx() {
    assert(this->buffer_pool_ != nullptr);
    this->cuda_nv12_frame_ctx_ = create_cuda_frame_ctx(AV_PIX_FMT_NV12);
    this->cuda_bgra_frame_ctx_ = create_cuda_frame_ctx(AV_PIX_FMT_BGRA);
}

void offload_rendering_client::ffmpeg_init_buffer_pool() {
    assert(this->buffer_pool_ != nullptr);
    avvk_color_frames_.resize(buffer_pool_->image_pool.size());
    avvk_depth_frames_.resize(buffer_pool_->image_pool.size());
    layout_transition_start_cmd_bufs_.resize(buffer_pool_->image_pool.size());
    layout_transition_end_cmd_bufs_.resize(buffer_pool_->image_pool.size());

    // Initialize frame resources for each buffer in the pool
    for (size_t i = 0; i < buffer_pool_->image_pool.size(); i++) {
        for (size_t eye = 0; eye < 2; eye++) {
            // Create and configure color frames
            auto vk_frame = av_vk_frame_alloc();
            if (!vk_frame) {
                throw std::runtime_error{"Failed to allocate FFmpeg Vulkan frame"};
            }
            vk_frame->img[0]          = buffer_pool_->image_pool[i][eye].image;
            vk_frame->tiling          = buffer_pool_->image_pool[i][eye].image_info.tiling;
            vk_frame->mem[0]          = buffer_pool_->image_pool[i][eye].allocation_info.deviceMemory;
            vk_frame->size[0]         = buffer_pool_->image_pool[i][eye].allocation_info.size;
            vk_frame->offset[0]       = static_cast<ptrdiff_t>(buffer_pool_->image_pool[i][eye].allocation_info.offset);
            vk_frame->queue_family[0] = display_provider_->queues_[vulkan::queue::GRAPHICS].family;

            // Create timeline semaphore for synchronization
            VkExportSemaphoreCreateInfo export_semaphore_create_info{VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO, nullptr,
                                                                     VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT};
            vk_frame->sem[0] =
                vulkan::create_timeline_semaphore(display_provider_->vk_device_, 0, &export_semaphore_create_info);
            vk_frame->sem_value[0] = 0;

            avvk_color_frames_[i][eye].vk_frame = vk_frame;

            // Create and configure AVFrame
            auto av_frame = av_frame_alloc();
            if (!av_frame) {
                throw std::runtime_error{"Failed to allocate FFmpeg frame"};
            }
            av_frame->format                 = AV_PIX_FMT_VULKAN;
            av_frame->width                  = static_cast<int>(buffer_pool_->image_pool[i][eye].image_info.extent.width);
            av_frame->height                 = static_cast<int>(buffer_pool_->image_pool[i][eye].image_info.extent.height);
            av_frame->hw_frames_ctx          = av_buffer_ref(frame_ctx_);
            av_frame->data[0]                = reinterpret_cast<uint8_t*>(vk_frame);
            av_frame->buf[0]                 = av_buffer_create(av_frame->data[0], 0, [](void*, uint8_t*) { }, nullptr, 0);
            av_frame->pts                    = 0;
            avvk_color_frames_[i][eye].frame = av_frame;

            // Create and configure depth frames if enabled
            if (use_depth_) {
                auto vk_depth_frame = av_vk_frame_alloc();
                if (!vk_depth_frame) {
                    throw std::runtime_error{"Failed to allocate FFmpeg Vulkan frame"};
                }
                vk_depth_frame->img[0]  = buffer_pool_->depth_image_pool[i][eye].image;
                vk_depth_frame->tiling  = buffer_pool_->depth_image_pool[i][eye].image_info.tiling;
                vk_depth_frame->mem[0]  = buffer_pool_->depth_image_pool[i][eye].allocation_info.deviceMemory;
                vk_depth_frame->size[0] = buffer_pool_->depth_image_pool[i][eye].allocation_info.size;
                vk_depth_frame->offset[0] =
                    static_cast<ptrdiff_t>(buffer_pool_->depth_image_pool[i][eye].allocation_info.offset);
                vk_depth_frame->queue_family[0] = display_provider_->queues_[vulkan::queue::GRAPHICS].family;

                vk_depth_frame->sem[0] =
                    vulkan::create_timeline_semaphore(display_provider_->vk_device_, 0, &export_semaphore_create_info);
                vk_depth_frame->sem_value[0] = 0;

                avvk_depth_frames_[i][eye].vk_frame = vk_depth_frame;

                auto av_depth_frame = av_frame_alloc();
                if (!av_depth_frame) {
                    throw std::runtime_error{"Failed to allocate FFmpeg frame"};
                }
                av_depth_frame->format = AV_PIX_FMT_VULKAN;
                av_depth_frame->width  = static_cast<int>(buffer_pool_->depth_image_pool[i][eye].image_info.extent.width);
                av_depth_frame->height = static_cast<int>(buffer_pool_->depth_image_pool[i][eye].image_info.extent.height);
                av_depth_frame->hw_frames_ctx = av_buffer_ref(frame_ctx_);
                av_depth_frame->data[0]       = reinterpret_cast<uint8_t*>(vk_depth_frame);
                av_depth_frame->buf[0] = av_buffer_create(av_depth_frame->data[0], 0, [](void*, uint8_t*) { }, nullptr, 0);
                av_depth_frame->pts    = 0;
                avvk_depth_frames_[i][eye].frame = av_depth_frame;
            }
        }
    }

    // Initialize decode frames
    for (size_t eye = 0; eye < 2; eye++) {
        // Initialize color decode frames
        decode_out_color_frames_[eye]                = av_frame_alloc();
        decode_out_color_frames_[eye]->format        = AV_PIX_FMT_CUDA;
        decode_out_color_frames_[eye]->hw_frames_ctx = av_buffer_ref(cuda_nv12_frame_ctx_);
        decode_out_color_frames_[eye]->width         = static_cast<int>(buffer_pool_->image_pool[0][0].image_info.extent.width);
        decode_out_color_frames_[eye]->height = static_cast<int>(buffer_pool_->image_pool[0][0].image_info.extent.height);
        auto ret                              = av_hwframe_get_buffer(cuda_nv12_frame_ctx_, decode_out_color_frames_[eye], 0);
        decode_out_color_frames_[eye]->color_range     = AVCOL_RANGE_JPEG;
        decode_out_color_frames_[eye]->colorspace      = AVCOL_SPC_BT709;
        decode_out_color_frames_[eye]->color_trc       = AVCOL_TRC_BT709;
        decode_out_color_frames_[eye]->color_primaries = AVCOL_PRI_BT709;
        AV_ASSERT_SUCCESS(ret);

        // Initialize color conversion frames
        decode_converted_color_frames_[eye]                = av_frame_alloc();
        decode_converted_color_frames_[eye]->format        = AV_PIX_FMT_CUDA;
        decode_converted_color_frames_[eye]->hw_frames_ctx = av_buffer_ref(cuda_bgra_frame_ctx_);
        decode_converted_color_frames_[eye]->width  = static_cast<int>(buffer_pool_->image_pool[0][0].image_info.extent.width);
        decode_converted_color_frames_[eye]->height = static_cast<int>(buffer_pool_->image_pool[0][0].image_info.extent.height);
        ret = av_hwframe_get_buffer(cuda_bgra_frame_ctx_, decode_converted_color_frames_[eye], 0);
        AV_ASSERT_SUCCESS(ret);

        // Initialize depth frames if enabled
        if (use_depth_) {
            decode_out_depth_frames_[eye]                = av_frame_alloc();
            decode_out_depth_frames_[eye]->format        = AV_PIX_FMT_CUDA;
            decode_out_depth_frames_[eye]->hw_frames_ctx = av_buffer_ref(cuda_nv12_frame_ctx_);
            decode_out_depth_frames_[eye]->width =
                static_cast<int>(buffer_pool_->depth_image_pool[0][0].image_info.extent.width);
            decode_out_depth_frames_[eye]->height =
                static_cast<int>(buffer_pool_->depth_image_pool[0][0].image_info.extent.height);
            decode_out_depth_frames_[eye]->color_range     = AVCOL_RANGE_JPEG;
            decode_out_depth_frames_[eye]->colorspace      = AVCOL_SPC_BT709;
            decode_out_depth_frames_[eye]->color_trc       = AVCOL_TRC_BT709;
            decode_out_depth_frames_[eye]->color_primaries = AVCOL_PRI_BT709;
            ret = av_hwframe_get_buffer(cuda_nv12_frame_ctx_, decode_out_depth_frames_[eye], 0);
            AV_ASSERT_SUCCESS(ret);

            decode_converted_depth_frames_[eye]                = av_frame_alloc();
            decode_converted_depth_frames_[eye]->format        = AV_PIX_FMT_CUDA;
            decode_converted_depth_frames_[eye]->hw_frames_ctx = av_buffer_ref(cuda_bgra_frame_ctx_);
            decode_converted_depth_frames_[eye]->width =
                static_cast<int>(buffer_pool_->depth_image_pool[0][0].image_info.extent.width);
            decode_converted_depth_frames_[eye]->height =
                static_cast<int>(buffer_pool_->depth_image_pool[0][0].image_info.extent.height);
            ret = av_hwframe_get_buffer(cuda_bgra_frame_ctx_, decode_converted_depth_frames_[eye], 0);
            AV_ASSERT_SUCCESS(ret);
        }
    }

    // Allocate NPP buffers for color space conversion
    yuv420_u_plane_ = nppiMalloc_8u_C1(static_cast<int>(buffer_pool_->image_pool[0][0].image_info.extent.width) / 2,
                                       static_cast<int>(buffer_pool_->image_pool[0][0].image_info.extent.height) / 2, &u_step_);
    yuv420_v_plane_ = nppiMalloc_8u_C1(static_cast<int>(buffer_pool_->image_pool[0][0].image_info.extent.width) / 2,
                                       static_cast<int>(buffer_pool_->image_pool[0][0].image_info.extent.height) / 2, &v_step_);
    yuv420_y_plane_ = nppiMalloc_8u_C1(static_cast<int>(buffer_pool_->image_pool[0][0].image_info.extent.width),
                                       static_cast<int>(buffer_pool_->image_pool[0][0].image_info.extent.height), &y_step_);
}

void offload_rendering_client::ffmpeg_init_decoder() {
    auto decoder = avcodec_find_decoder_by_name(OFFLOAD_RENDERING_FFMPEG_DECODER_NAME);
    if (!decoder) {
        throw std::runtime_error{"Failed to find FFmpeg decoder"};
    }

    // Initialize color decoder
    this->codec_color_ctx_ = avcodec_alloc_context3(decoder);
    if (!codec_color_ctx_) {
        throw std::runtime_error{"Failed to allocate FFmpeg decoder context"};
    }

    // Configure decoder parameters
    codec_color_ctx_->thread_count  = 0; // auto
    codec_color_ctx_->thread_type   = FF_THREAD_SLICE;
    codec_color_ctx_->pix_fmt       = AV_PIX_FMT_CUDA;
    codec_color_ctx_->sw_pix_fmt    = AV_PIX_FMT_NV12;
    codec_color_ctx_->hw_device_ctx = av_buffer_ref(cuda_device_ctx_);
    codec_color_ctx_->hw_frames_ctx = av_buffer_ref(cuda_nv12_frame_ctx_);
    codec_color_ctx_->width         = static_cast<int>(buffer_pool_->image_pool[0][0].image_info.extent.width);
    codec_color_ctx_->height        = static_cast<int>(buffer_pool_->image_pool[0][0].image_info.extent.height);
    codec_color_ctx_->framerate     = {0, 1};
    codec_color_ctx_->flags |= AV_CODEC_FLAG_LOW_DELAY;
    codec_color_ctx_->color_range     = AVCOL_RANGE_JPEG;
    codec_color_ctx_->colorspace      = AVCOL_SPC_BT709;
    codec_color_ctx_->color_trc       = AVCOL_TRC_BT709;
    codec_color_ctx_->color_primaries = AVCOL_PRI_BT709;

    // Set zero latency mode
    av_opt_set_int(codec_color_ctx_->priv_data, "zerolatency", 1, 0);
    av_opt_set_int(codec_color_ctx_->priv_data, "delay", 0, 0);
    av_opt_set(codec_color_ctx_->priv_data, "hwaccel", "cuda", 0);

    auto ret = avcodec_open2(codec_color_ctx_, decoder, nullptr);
    AV_ASSERT_SUCCESS(ret);

    // Initialize depth decoder if enabled
    if (use_depth_) {
        this->codec_depth_ctx_ = avcodec_alloc_context3(decoder);
        if (!codec_depth_ctx_) {
            throw std::runtime_error{"Failed to allocate FFmpeg decoder context"};
        }

        // Configure depth decoder parameters (same as color)
        codec_depth_ctx_->thread_count  = 0;
        codec_depth_ctx_->thread_type   = FF_THREAD_SLICE;
        codec_depth_ctx_->pix_fmt       = AV_PIX_FMT_CUDA;
        codec_depth_ctx_->sw_pix_fmt    = AV_PIX_FMT_NV12;
        codec_depth_ctx_->hw_device_ctx = av_buffer_ref(cuda_device_ctx_);
        codec_depth_ctx_->hw_frames_ctx = av_buffer_ref(cuda_nv12_frame_ctx_);
        codec_depth_ctx_->width         = static_cast<int>(buffer_pool_->image_pool[0][0].image_info.extent.width);
        codec_depth_ctx_->height        = static_cast<int>(buffer_pool_->image_pool[0][0].image_info.extent.height);
        codec_depth_ctx_->framerate     = {0, 1};
        codec_depth_ctx_->flags |= AV_CODEC_FLAG_LOW_DELAY;
        codec_depth_ctx_->color_range     = AVCOL_RANGE_JPEG;
        codec_depth_ctx_->colorspace      = AVCOL_SPC_BT709;
        codec_depth_ctx_->color_trc       = AVCOL_TRC_BT709;
        codec_depth_ctx_->color_primaries = AVCOL_PRI_BT709;

        av_opt_set_int(codec_depth_ctx_->priv_data, "zerolatency", 1, 0);
        av_opt_set_int(codec_depth_ctx_->priv_data, "delay", 0, 0);
        av_opt_set(codec_depth_ctx_->priv_data, "hwaccel", "cuda", 0);

        ret = avcodec_open2(codec_depth_ctx_, decoder, nullptr);
        AV_ASSERT_SUCCESS(ret);
    }
}
