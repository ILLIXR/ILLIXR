#include "illixr/data_format.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/serializable_data.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"
#include "illixr/vk/display_provider.hpp"
#include "illixr/vk/ffmpeg_utils.hpp"
#include "illixr/vk/render_pass.hpp"
#include "illixr/vk/vk_extension_request.h"
#include "illixr/vk/vulkan_utils.hpp"

extern "C" {
#include "libavfilter_illixr/buffersink.h"
#include "libavfilter_illixr/buffersrc.h"
#include "libswscale_illixr/swscale.h"
}

#include "illixr/pose_prediction.hpp"
#include "nppi.h"

#include <cstdlib>
#include <set>

#define OFFLOAD_RENDERING_FFMPEG_DECODER_NAME "h264"

using namespace ILLIXR;
using namespace ILLIXR::vulkan::ffmpeg_utils;

class offload_rendering_client
    : public threadloop
    , public vulkan::app {
public:
    offload_rendering_client(const std::string& name, phonebook* pb)
        : threadloop{name, pb}
        , sb{pb->lookup_impl<switchboard>()}
        , log{spdlogger(nullptr)}
        , dp{pb->lookup_impl<vulkan::display_provider>()}
        , frames_reader{sb->get_buffered_reader<compressed_frame>("compressed_frames")}
        , pose_writer{sb->get_network_writer<fast_pose_type>("render_pose", {})}
        , pp{pb->lookup_impl<pose_prediction>()}
        , clock{pb->lookup_impl<RelativeClock>()} {
        display_provider_ffmpeg = dp;

        use_depth = std::getenv("ILLIXR_USE_DEPTH_IMAGES") != nullptr && std::stoi(std::getenv("ILLIXR_USE_DEPTH_IMAGES"));
        if (use_depth) {
            log->debug("Encoding depth images for the client");
        } else {
            log->debug("Not encoding depth images for the client");
        }
    }

    void start() override {
        ffmpeg_init_device();
        ffmpeg_init_cuda_device();
        threadloop::start();
    }

    virtual void setup(VkRenderPass render_pass, uint32_t subpass,
                       std::shared_ptr<vulkan::buffer_pool<fast_pose_type>> buffer_pool) override {
        this->buffer_pool = buffer_pool;
        command_pool      = vulkan::create_command_pool(dp->vk_device, dp->queues[vulkan::queue::GRAPHICS].family);
        ffmpeg_init_frame_ctx();
        ffmpeg_init_cuda_frame_ctx();
        ffmpeg_init_buffer_pool();
        ffmpeg_init_decoder();
        ready = true;

        for (auto& frame : avvk_color_frames) {
            for (auto& eye : frame) {
                auto cmd_buf = vulkan::begin_one_time_command(dp->vk_device, command_pool);
                transition_layout(cmd_buf, eye.frame, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                vulkan::end_one_time_command(dp->vk_device, command_pool, dp->queues[vulkan::queue::GRAPHICS], cmd_buf);
            }
        }

        if (use_depth) {
            for (auto& frame : avvk_depth_frames) {
                for (auto& eye : frame) {
                    auto cmd_buf = vulkan::begin_one_time_command(dp->vk_device, command_pool);
                    transition_layout(cmd_buf, eye.frame, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                    vulkan::end_one_time_command(dp->vk_device, command_pool, dp->queues[vulkan::queue::GRAPHICS], cmd_buf);
                }
            }
        }

        for (size_t i = 0; i < avvk_color_frames.size(); i++) {
            for (auto eye = 0; eye < 2; eye++) {
                layout_transition_start_cmd_bufs[i][eye] = vulkan::create_command_buffer(dp->vk_device, command_pool);
                VkCommandBufferBeginInfo begin_info{};
                begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                begin_info.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
                vkBeginCommandBuffer(layout_transition_start_cmd_bufs[i][eye], &begin_info);
                transition_layout(layout_transition_start_cmd_bufs[i][eye], avvk_color_frames[i][eye].frame,
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
                if (use_depth) {
                    transition_layout(layout_transition_start_cmd_bufs[i][eye], avvk_depth_frames[i][eye].frame,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
                }
                vkEndCommandBuffer(layout_transition_start_cmd_bufs[i][eye]);

                layout_transition_end_cmd_bufs[i][eye] = vulkan::create_command_buffer(dp->vk_device, command_pool);
                vkBeginCommandBuffer(layout_transition_end_cmd_bufs[i][eye], &begin_info);
                transition_layout(layout_transition_end_cmd_bufs[i][eye], avvk_color_frames[i][eye].frame,
                                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                if (use_depth) {
                    transition_layout(layout_transition_end_cmd_bufs[i][eye], avvk_depth_frames[i][eye].frame,
                                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                }
                vkEndCommandBuffer(layout_transition_end_cmd_bufs[i][eye]);
            }
        }

        VkFenceCreateInfo fence_info = {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        };

        vkCreateFence(dp->vk_device, &fence_info, nullptr, &fence);
    }

    void record_command_buffer(VkCommandBuffer commandBuffer, VkFramebuffer framebuffer, int buffer_ind, bool left) override {}
    void update_uniforms(const pose_type& render_pose) override {}

    bool is_external() override {
        return true;
    }

    void destroy() override {
        for (auto& frame : avvk_color_frames) {
            for (auto& eye : frame) {
                av_frame_free(&eye.frame);
            }
        }

        for (auto& frame : avvk_depth_frames) {
            for (auto& eye : frame) {
                av_frame_free(&eye.frame);
            }
        }
        av_buffer_unref(&frame_ctx);
        av_buffer_unref(&device_ctx);
    }

protected:
    void _p_thread_setup() override { }

    skip_option _p_should_skip() override {
        return threadloop::_p_should_skip();
    }

    void copy_image_to_cpu_and_save_file(AVFrame* frame) {
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

        std::string filename = "frame_" + std::to_string(frame_count) + ".png";
        FILE*       f        = fopen(filename.c_str(), "wb");
        fwrite(png_packet->data, 1, png_packet->size, f);
        fclose(f);

        av_packet_free(&png_packet);
        av_frame_free(&cpu_av_frame);
        avcodec_free_context(&png_codec_ctx);
    }

    void save_nv12_img_to_png(AVFrame* cuda_frame) {
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

        std::string filename = "frame_" + std::to_string(frame_count) + ".png";
        FILE*       f        = fopen(filename.c_str(), "wb");
        fwrite(png_packet->data, 1, png_packet->size, f);
        fclose(f);

        av_packet_free(&png_packet);
        av_frame_free(&cpu_av_frame);
        avcodec_free_context(&png_codec_ctx);
    }

    // Vulkan layout transition
    // supports: shader read <-> transfer dst
    void transition_layout(VkCommandBuffer cmd_buf, AVFrame* frame, VkImageLayout old_layout, VkImageLayout new_layout) {
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
        } else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
                   new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
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

    void _p_one_iteration() override {
        if (!ready) {
            return;
        }
        push_pose();
        std::cout << "Pushing network pose" << std::endl;
        if (!network_receive()) {
            return;
        }

        // system timestamp
        auto timestamp = std::chrono::high_resolution_clock::now();
        auto diff      = timestamp - decoded_frame_pose.predict_target_time._m_time_since_epoch;
        // log->info("diff (ms): {}", diff.time_since_epoch().count() / 1000000.0);

        // log->debug("Position: {}, {}, {}", pose.position[0], pose.position[1], pose.position[2]);

        auto decode_start = std::chrono::high_resolution_clock::now();
        // receive packets
        for (auto eye = 0; eye < 2; eye++) {
            auto ret = avcodec_send_packet(codec_color_ctx, decode_src_color_packets[eye]);
            if (ret == AVERROR(EAGAIN)) {
                throw std::runtime_error{"FFmpeg encoder returned EAGAIN. Internal buffer full? Try using a higher-end GPU."};
            }
            AV_ASSERT_SUCCESS(ret);
            // ret = avcodec_receive_frame(codec_ctx, decode_out_frames[eye]);
            // AV_ASSERT_SUCCESS(ret);
            // copy_image_to_cpu_and_save_file(decode_out_frames[eye]);
            // decode_out_frames[eye]->pts = frame_count++;

            if (use_depth) {
                ret = avcodec_send_packet(codec_depth_ctx, decode_src_depth_packets[eye]);
                if (ret == AVERROR(EAGAIN)) {
                    throw std::runtime_error{"FFmpeg encoder returned EAGAIN. Internal buffer full? Try using a higher-end GPU."};
                }
                AV_ASSERT_SUCCESS(ret);
            }
        }
        for (auto eye = 0; eye < 2; eye++) {
            auto ret = avcodec_receive_frame(codec_color_ctx, decode_out_color_frames[eye]);
            assert(decode_out_color_frames[eye]->format == AV_PIX_FMT_CUDA);
            AV_ASSERT_SUCCESS(ret);

            if (use_depth) {
                ret = avcodec_receive_frame(codec_depth_ctx, decode_out_depth_frames[eye]);
                assert(decode_out_depth_frames[eye]->format == AV_PIX_FMT_CUDA);
                AV_ASSERT_SUCCESS(ret);
            }
        }
        auto decode_end = std::chrono::high_resolution_clock::now();

        for (auto eye = 0; eye < 2; eye++) {
            NppiSize roi = {static_cast<int>(decode_out_color_frames[eye]->width), static_cast<int>(decode_out_color_frames[eye]->height)};
            Npp8u*   pSrc[2];
            pSrc[0] = reinterpret_cast<Npp8u*>(decode_out_color_frames[eye]->data[0]);
            pSrc[1] = reinterpret_cast<Npp8u*>(decode_out_color_frames[eye]->data[1]);
            Npp8u* pDst[3];
            pDst[0] = yuv420_y_plane;
            pDst[1] = yuv420_u_plane;
            pDst[2] = yuv420_v_plane;
            int dst_linesizes[3];
            dst_linesizes[0] = y_step;
            dst_linesizes[1] = u_step;
            dst_linesizes[2] = v_step;
            auto ret              = nppiNV12ToYUV420_8u_P2P3R(pSrc, decode_out_color_frames[eye]->linesize[0], pDst, dst_linesizes, roi);
            assert(ret == NPP_SUCCESS);
            auto dst = reinterpret_cast<Npp8u*>(decode_converted_color_frames[eye]->data[0]);
            ret      = nppiYUV420ToBGR_8u_P3C4R(pDst, dst_linesizes, dst, decode_converted_color_frames[eye]->linesize[0], roi);
            assert(ret == NPP_SUCCESS);
            if (use_depth) {
                NppiSize roi_depth = {static_cast<int>(decode_out_depth_frames[eye]->width), static_cast<int>(decode_out_depth_frames[eye]->height)};
                Npp8u*   pSrc_depth[2];
                pSrc_depth[0] = reinterpret_cast<Npp8u*>(decode_out_depth_frames[eye]->data[0]);
                pSrc_depth[1] = reinterpret_cast<Npp8u*>(decode_out_depth_frames[eye]->data[1]);
                // Npp8u* pDst_depth[3];
                // pDst_depth[0] = yuv420_y_plane;
                // pDst_depth[1] = yuv420_u_plane;
                // pDst_depth[2] = yuv420_v_plane;
                // int dst_linesizes_depth[3];
                // dst_linesizes[0] = y_step;
                // dst_linesizes[1] = u_step;
                // dst_linesizes[2] = v_step;
                ret              = nppiNV12ToYUV420_8u_P2P3R(pSrc_depth, decode_out_depth_frames[eye]->linesize[0], pDst, dst_linesizes, roi);
                assert(ret == NPP_SUCCESS);
                auto dst_depth = reinterpret_cast<Npp8u*>(decode_converted_depth_frames[eye]->data[0]);
                ret      = nppiYUV420ToBGR_8u_P3C4R(pDst, dst_linesizes, dst_depth, decode_converted_depth_frames[eye]->linesize[0], roi);
                assert(ret == NPP_SUCCESS);
            }
            cudaDeviceSynchronize();
        }
        auto conversion_end = std::chrono::high_resolution_clock::now();

        auto ind            = buffer_pool->src_acquire_image();
        std::cout << "SRC Acquired image " << static_cast<int>(ind) << std::endl;
        auto transfer_start = std::chrono::high_resolution_clock::now();

        auto* frames = reinterpret_cast<AVHWFramesContext*>(frame_ctx->data);
        auto* vk = static_cast<AVVulkanFramesContext*>(frames->hwctx);

        for (auto eye = 0; eye < 2; eye++) {
            // save_nv12_img_to_png(decode_out_frames[eye]);
            vk->lock_frame(frames, avvk_color_frames[ind][eye].vk_frame);
            if (use_depth) {
                vk->lock_frame(frames, avvk_depth_frames[ind][eye].vk_frame);
            }

            vkResetFences(dp->vk_device, 1, &fence);

            std::vector<VkSemaphore> timelines = {avvk_color_frames[ind][eye].vk_frame->sem[0]};
            std::vector<VkPipelineStageFlags> wait_stages = {VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT};
            if (use_depth) {
                timelines.push_back(avvk_depth_frames[ind][eye].vk_frame->sem[0]);
                wait_stages.push_back(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
            }

            std::vector<uint64_t> start_wait_values = {avvk_color_frames[ind][eye].vk_frame->sem_value[0]};
            std::vector<uint64_t> start_signal_values = {++avvk_color_frames[ind][eye].vk_frame->sem_value[0]};
            if (use_depth) {
                start_wait_values.push_back(avvk_depth_frames[ind][eye].vk_frame->sem_value[0]);
                start_signal_values.push_back(++avvk_depth_frames[ind][eye].vk_frame->sem_value[0]);
            }

            VkTimelineSemaphoreSubmitInfo transition_start_timeline = {
                .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
                .pNext = nullptr,
                .waitSemaphoreValueCount = static_cast<uint16_t>(start_wait_values.size()),
                .pWaitSemaphoreValues = start_wait_values.data(),
                .signalSemaphoreValueCount = static_cast<uint16_t>(start_signal_values.size()),
                .pSignalSemaphoreValues = start_signal_values.data(),
            };

            VkSubmitInfo transition_start_submit = {
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                .pNext = &transition_start_timeline,
                .waitSemaphoreCount = static_cast<uint16_t>(timelines.size()),
                .pWaitSemaphores = timelines.data(),
                .pWaitDstStageMask = wait_stages.data(),
                .commandBufferCount = 1,
                .pCommandBuffers = &layout_transition_start_cmd_bufs[ind][eye],
                .signalSemaphoreCount = static_cast<uint16_t>(timelines.size()),
                .pSignalSemaphores = timelines.data(),
            };
//            submit_command_buffer(layout_transition_start_cmd_bufs[ind][eye]);
            std::cout << "Starting transition" << std::endl;
            vulkan::locked_queue_submit(dp->queues[vulkan::queue::GRAPHICS], 1, &transition_start_submit, nullptr);

            auto ret = av_hwframe_transfer_data(avvk_color_frames[ind][eye].frame, decode_converted_color_frames[eye], 0);
            AV_ASSERT_SUCCESS(ret);

            if (use_depth) {
                ret = av_hwframe_transfer_data(avvk_depth_frames[ind][eye].frame, decode_converted_depth_frames[eye], 0);
            }
            AV_ASSERT_SUCCESS(ret);

            std::vector<uint64_t> end_wait_values = {avvk_color_frames[ind][eye].vk_frame->sem_value[0]};
            std::vector<uint64_t> end_signal_values = {++avvk_color_frames[ind][eye].vk_frame->sem_value[0]};
            if (use_depth) {
                end_wait_values.push_back(avvk_depth_frames[ind][eye].vk_frame->sem_value[0]);
                end_signal_values.push_back(++avvk_depth_frames[ind][eye].vk_frame->sem_value[0]);
            }

            VkTimelineSemaphoreSubmitInfo transition_end_timeline = {
                .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
                .pNext = nullptr,
                .waitSemaphoreValueCount = static_cast<uint16_t>(end_wait_values.size()),
                .pWaitSemaphoreValues = end_wait_values.data(),
                .signalSemaphoreValueCount = static_cast<uint16_t>(end_signal_values.size()),
                .pSignalSemaphoreValues = end_signal_values.data(),
            };

            VkSubmitInfo transition_end_submit = {
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                .pNext = &transition_end_timeline,
                .waitSemaphoreCount = static_cast<uint16_t>(timelines.size()),
                .pWaitSemaphores = timelines.data(),
                .pWaitDstStageMask = wait_stages.data(),
                .commandBufferCount = 1,
                .pCommandBuffers = &layout_transition_end_cmd_bufs[ind][eye],
                .signalSemaphoreCount = static_cast<uint16_t>(timelines.size()),
                .pSignalSemaphores = timelines.data(),
            };
            //            submit_command_buffer(layout_transition_end_cmd_bufs[ind][eye]);
            std::cout << "Ending transition" << std::endl;
            vulkan::locked_queue_submit(dp->queues[vulkan::queue::GRAPHICS], 1, &transition_end_submit, fence);
            vkWaitForFences(dp->vk_device, 1, &fence, VK_TRUE, UINT64_MAX);

            if (use_depth) {
                decode_out_color_frames[eye]->pts = frame_count++;
                decode_out_depth_frames[eye]->pts = frame_count++;

                vulkan::wait_timeline_semaphores(dp->vk_device, {{avvk_color_frames[ind][eye].vk_frame->sem[0], avvk_color_frames[ind][eye].vk_frame->sem_value[0]},
                                                                 {avvk_depth_frames[ind][eye].vk_frame->sem[0], avvk_depth_frames[ind][eye].vk_frame->sem_value[0]}});

                vk->unlock_frame(frames, avvk_color_frames[ind][eye].vk_frame);
                vk->unlock_frame(frames, avvk_depth_frames[ind][eye].vk_frame);
            } else {
                decode_out_color_frames[eye]->pts = frame_count++;

                vulkan::wait_timeline_semaphores(dp->vk_device, {{avvk_color_frames[ind][eye].vk_frame->sem[0], avvk_color_frames[ind][eye].vk_frame->sem_value[0]}});

                vk->unlock_frame(frames, avvk_color_frames[ind][eye].vk_frame);
            }
        }

//        if (use_depth) {
//            vulkan::wait_timeline_semaphores(dp->vk_device, {{avvk_color_frames[ind][0].vk_frame->sem[0], avvk_color_frames[ind][0].vk_frame->sem_value[0]},
//                                                         {avvk_color_frames[ind][1].vk_frame->sem[0], avvk_color_frames[ind][1].vk_frame->sem_value[0]},
//                                                         {avvk_depth_frames[ind][0].vk_frame->sem[0], avvk_depth_frames[ind][0].vk_frame->sem_value[0]},
//                                                         {avvk_depth_frames[ind][1].vk_frame->sem[0], avvk_depth_frames[ind][1].vk_frame->sem_value[0]}});
//        } else {
//            vulkan::wait_timeline_semaphores(dp->vk_device, {{avvk_color_frames[ind][0].vk_frame->sem[0], avvk_color_frames[ind][0].vk_frame->sem_value[0]},
//                                                         {avvk_color_frames[ind][1].vk_frame->sem[0], avvk_color_frames[ind][1].vk_frame->sem_value[0]}});
//        }

        auto transfer_end = std::chrono::high_resolution_clock::now();
        std::cout << "SRC Releasing image " << static_cast<int>(ind) << std::endl;
        buffer_pool->src_release_image(ind, std::move(decoded_frame_pose));
        log->info("decode (microseconds): {}\n conversion (microseconds): {}\n transfer (microseconds): {}",
                  std::chrono::duration_cast<std::chrono::microseconds>(decode_end - decode_start).count(),
                  std::chrono::duration_cast<std::chrono::microseconds>(conversion_end - decode_end).count(),
                  std::chrono::duration_cast<std::chrono::microseconds>(transfer_end - transfer_start).count());

        metrics["decode"] += std::chrono::duration_cast<std::chrono::microseconds>(decode_end - decode_start).count();
        metrics["conversion"] += std::chrono::duration_cast<std::chrono::microseconds>(conversion_end - decode_end).count();
        metrics["transfer"] += std::chrono::duration_cast<std::chrono::microseconds>(transfer_end - transfer_start).count();

        if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - fps_start_time)
                .count() >= 1) {
            log->info("Decoder FPS: {}", fps_counter);
            fps_start_time = std::chrono::high_resolution_clock::now();

            for (auto& metric : metrics) {
                auto fps = std::max(fps_counter, (uint16_t) 0);
                log->info("{}: {}", metric.first, metric.second / (double) (fps));
                metric.second = 0;
            }
            fps_counter    = 0;
        } else {
            fps_counter++;
        }
    }

private:
    std::shared_ptr<switchboard>                   sb;
    std::shared_ptr<spdlog::logger>                log;
    std::shared_ptr<vulkan::display_provider>      dp;
    switchboard::buffered_reader<compressed_frame> frames_reader;
    switchboard::network_writer<fast_pose_type>    pose_writer;
    std::shared_ptr<pose_prediction>               pp;
    std::atomic<bool>                              ready = false;

    std::shared_ptr<vulkan::buffer_pool<fast_pose_type>> buffer_pool;
    bool use_depth = false;
    std::vector<std::array<ffmpeg_vk_frame, 2>>          avvk_color_frames;
    std::vector<std::array<ffmpeg_vk_frame, 2>>          avvk_depth_frames;
    std::vector<std::array<VkCommandBuffer, 2>>          layout_transition_start_cmd_bufs;
    std::vector<std::array<VkCommandBuffer, 2>>          layout_transition_end_cmd_bufs;
    AVBufferRef*                                         device_ctx          = nullptr;
    AVBufferRef*                                         cuda_device_ctx     = nullptr;
    AVBufferRef*                                         frame_ctx           = nullptr;
    AVBufferRef*                                         cuda_nv12_frame_ctx = nullptr;
    AVBufferRef*                                         cuda_bgra_frame_ctx = nullptr;

    AVCodecContext*          codec_color_ctx               = nullptr;
    std::array<AVPacket*, 2> decode_src_color_packets      = {nullptr, nullptr};
    std::array<AVFrame*, 2>  decode_out_color_frames       = {nullptr, nullptr};
    std::array<AVFrame*, 2>  decode_converted_color_frames = {nullptr, nullptr};

    AVCodecContext*          codec_depth_ctx               = nullptr;
    std::array<AVPacket*, 2> decode_src_depth_packets      = {nullptr, nullptr};
    std::array<AVFrame*, 2>  decode_out_depth_frames       = {nullptr, nullptr};
    std::array<AVFrame*, 2>  decode_converted_depth_frames = {nullptr, nullptr};

    fast_pose_type decoded_frame_pose;

    VkCommandPool command_pool{};
    Npp8u*        yuv420_y_plane;
    Npp8u*        yuv420_u_plane;
    Npp8u*        yuv420_v_plane;
    int           y_step;
    int           u_step;
    int           v_step;

    uint64_t frame_count = 0;

    VkFence fence;

    uint16_t                                       fps_counter    = 0;
    std::chrono::high_resolution_clock::time_point fps_start_time = std::chrono::high_resolution_clock::now();
    std::map<std::string, uint32_t> metrics;

    void submit_command_buffer(VkCommandBuffer vk_command_buffer) {

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
        vulkan::locked_queue_submit(dp->queues[vulkan::queue::GRAPHICS], 1, &submitInfo, nullptr);
    }

    void push_pose() {
        auto current_pose = pp->get_fast_pose();
        auto now =
            time_point{std::chrono::duration<long, std::nano>{std::chrono::high_resolution_clock::now().time_since_epoch()}};
        current_pose.predict_target_time   = now;
        current_pose.predict_computed_time = now;
        pose_writer.put(std::make_shared<fast_pose_type>(current_pose));
    }

    bool network_receive() {
        if (decode_src_color_packets[0] != nullptr) {
            av_packet_free_side_data(decode_src_color_packets[0]);
            av_packet_free_side_data(decode_src_color_packets[1]);
            av_packet_free(&decode_src_color_packets[0]);
            av_packet_free(&decode_src_color_packets[1]);
            if (use_depth) {
                av_packet_free_side_data(decode_src_depth_packets[0]);
                av_packet_free_side_data(decode_src_depth_packets[1]);
                av_packet_free(&decode_src_depth_packets[0]);
                av_packet_free(&decode_src_depth_packets[1]);
            }
        }
        auto frame            = frames_reader.dequeue();
        if (frame == nullptr) {
            return false;
        }
        decode_src_color_packets[0] = frame->left_color;
        decode_src_color_packets[1] = frame->right_color;
        if (use_depth) {
            decode_src_depth_packets[0] = frame->left_depth;
        decode_src_depth_packets[1] = frame->right_depth;
        }
        // log->info("Received frame {}", frame_count);
        uint64_t timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
        auto diff_ns = timestamp - frame->sent_time;
        log->info("diff (ms): {}", diff_ns / 1000000.0);
        decoded_frame_pose = frame->pose;
        return true;
    }

    void ffmpeg_init_device() {
        this->device_ctx      = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_VULKAN);
        auto hwdev_ctx        = reinterpret_cast<AVHWDeviceContext*>(device_ctx->data);
        auto vulkan_hwdev_ctx = reinterpret_cast<AVVulkanDeviceContext*>(hwdev_ctx->hwctx);

        vulkan_hwdev_ctx->inst            = dp->vk_instance;
        vulkan_hwdev_ctx->phys_dev        = dp->vk_physical_device;
        vulkan_hwdev_ctx->act_dev         = dp->vk_device;
        vulkan_hwdev_ctx->device_features = dp->features;
        for (auto& queue : dp->queues) {
            switch (queue.first) {
            case vulkan::queue::GRAPHICS:
                vulkan_hwdev_ctx->queue_family_index    = queue.second.family;
                vulkan_hwdev_ctx->nb_graphics_queues    = 1;
                vulkan_hwdev_ctx->queue_family_tx_index = queue.second.family;
                vulkan_hwdev_ctx->nb_tx_queues          = 1;
                break;
            case vulkan::queue::COMPUTE:
                vulkan_hwdev_ctx->queue_family_comp_index = queue.second.family;
                vulkan_hwdev_ctx->nb_comp_queues          = 1;
            default:
                break;
            }
        }

        if (dp->queues.find(vulkan::queue::DEDICATED_TRANSFER) != dp->queues.end()) {
            vulkan_hwdev_ctx->queue_family_tx_index = dp->queues[vulkan::queue::DEDICATED_TRANSFER].family;
            vulkan_hwdev_ctx->nb_tx_queues          = 1;
        }

        // not using Vulkan Video for now
        vulkan_hwdev_ctx->nb_encode_queues          = 0;
        vulkan_hwdev_ctx->nb_decode_queues          = 0;
        vulkan_hwdev_ctx->queue_family_encode_index = -1;
        vulkan_hwdev_ctx->queue_family_decode_index = -1;

        vulkan_hwdev_ctx->alloc         = nullptr;
        vulkan_hwdev_ctx->get_proc_addr = vkGetInstanceProcAddr;

        vulkan_hwdev_ctx->enabled_inst_extensions    = dp->enabled_instance_extensions.data();
        vulkan_hwdev_ctx->nb_enabled_inst_extensions = dp->enabled_instance_extensions.size();
        vulkan_hwdev_ctx->enabled_dev_extensions     = dp->enabled_device_extensions.data();
        vulkan_hwdev_ctx->nb_enabled_dev_extensions  = dp->enabled_device_extensions.size();

        vulkan_hwdev_ctx->lock_queue   = &ffmpeg_lock_queue;
        vulkan_hwdev_ctx->unlock_queue = &ffmpeg_unlock_queue;

        AV_ASSERT_SUCCESS(av_hwdevice_ctx_init(device_ctx));
        log->info("FFmpeg Vulkan hwdevice context initialized");
    }

    void ffmpeg_init_cuda_device() {
        auto ret = av_hwdevice_ctx_create(&cuda_device_ctx, AV_HWDEVICE_TYPE_CUDA, nullptr, nullptr, 0);
        AV_ASSERT_SUCCESS(ret);
        if (cuda_device_ctx == nullptr) {
            throw std::runtime_error{"Failed to create FFmpeg CUDA hwdevice context"};
        }

        log->info("FFmpeg CUDA hwdevice context initialized");
    }

    void ffmpeg_init_frame_ctx() {
        assert(this->buffer_pool != nullptr);
        this->frame_ctx = av_hwframe_ctx_alloc(device_ctx);
        if (!frame_ctx) {
            throw std::runtime_error{"Failed to create FFmpeg Vulkan hwframe context"};
        }

        auto hwframe_ctx    = reinterpret_cast<AVHWFramesContext*>(frame_ctx->data);
        hwframe_ctx->format = AV_PIX_FMT_VULKAN;
        auto pix_format = vulkan::ffmpeg_utils::get_pix_format_from_vk_format(buffer_pool->image_pool[0][0].image_info.format);
        if (!pix_format) {
            throw std::runtime_error{"Unsupported Vulkan image format when creating FFmpeg Vulkan hwframe context"};
        }
        assert(pix_format == AV_PIX_FMT_BGRA);
        hwframe_ctx->sw_format         = AV_PIX_FMT_BGRA;
        hwframe_ctx->width             = buffer_pool->image_pool[0][0].image_info.extent.width;
        hwframe_ctx->height            = buffer_pool->image_pool[0][0].image_info.extent.height;
        hwframe_ctx->initial_pool_size = 0;
        auto ret                       = av_hwframe_ctx_init(frame_ctx);
        AV_ASSERT_SUCCESS(ret);
    }

    AVBufferRef* create_cuda_frame_ctx(AVPixelFormat fmt) {
        auto cuda_frame_ref = av_hwframe_ctx_alloc(cuda_device_ctx);
        if (!cuda_frame_ref) {
            throw std::runtime_error{"Failed to create FFmpeg CUDA hwframe context"};
        }
        auto cuda_hwframe_ctx               = reinterpret_cast<AVHWFramesContext*>(cuda_frame_ref->data);
        cuda_hwframe_ctx->format            = AV_PIX_FMT_CUDA;
        cuda_hwframe_ctx->sw_format         = fmt;
        cuda_hwframe_ctx->width             = buffer_pool->image_pool[0][0].image_info.extent.width;
        cuda_hwframe_ctx->height            = buffer_pool->image_pool[0][0].image_info.extent.height;
        cuda_hwframe_ctx->initial_pool_size = 0;
        auto ret                            = av_hwframe_ctx_init(cuda_frame_ref);
        AV_ASSERT_SUCCESS(ret);
        return cuda_frame_ref;
    }

    void ffmpeg_init_cuda_frame_ctx() {
        assert(this->buffer_pool != nullptr);
        this->cuda_nv12_frame_ctx = create_cuda_frame_ctx(AV_PIX_FMT_NV12);
        this->cuda_bgra_frame_ctx = create_cuda_frame_ctx(AV_PIX_FMT_BGRA);
    }

    void ffmpeg_init_buffer_pool() {
        assert(this->buffer_pool != nullptr);
        avvk_color_frames.resize(buffer_pool->image_pool.size());
        avvk_depth_frames.resize(buffer_pool->image_pool.size());
        layout_transition_start_cmd_bufs.resize(buffer_pool->image_pool.size());
        layout_transition_end_cmd_bufs.resize(buffer_pool->image_pool.size());
        for (size_t i = 0; i < buffer_pool->image_pool.size(); i++) {
            for (size_t eye = 0; eye < 2; eye++) {
                // Create AVVkFrame
                auto vk_frame = av_vk_frame_alloc();
                if (!vk_frame) {
                    throw std::runtime_error{"Failed to allocate FFmpeg Vulkan frame"};
                }
                // The image index is just 0 here for AVVKFrame since we're not using multi-plane
                vk_frame->img[0]          = buffer_pool->image_pool[i][eye].image;
                vk_frame->tiling          = buffer_pool->image_pool[i][eye].image_info.tiling;
                vk_frame->mem[0]          = buffer_pool->image_pool[i][eye].allocation_info.deviceMemory;
                vk_frame->size[0]         = buffer_pool->image_pool[i][eye].allocation_info.size;
                vk_frame->offset[0]       = buffer_pool->image_pool[i][eye].allocation_info.offset;
                vk_frame->queue_family[0] = dp->queues[vulkan::queue::GRAPHICS].family;

                VkExportSemaphoreCreateInfo export_semaphore_create_info{
                    VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO, nullptr, VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT};
                vk_frame->sem[0]       = vulkan::create_timeline_semaphore(dp->vk_device, 0, &export_semaphore_create_info);
                vk_frame->sem_value[0] = 0;

                avvk_color_frames[i][eye].vk_frame = vk_frame;

                // Create AVFrame
                auto av_frame = av_frame_alloc();
                if (!av_frame) {
                    throw std::runtime_error{"Failed to allocate FFmpeg frame"};
                }
                av_frame->format        = AV_PIX_FMT_VULKAN;
                av_frame->width         = buffer_pool->image_pool[i][eye].image_info.extent.width;
                av_frame->height        = buffer_pool->image_pool[i][eye].image_info.extent.height;
                av_frame->hw_frames_ctx = av_buffer_ref(frame_ctx);
                av_frame->data[0]       = reinterpret_cast<uint8_t*>(vk_frame);
                av_frame->buf[0]        = av_buffer_create(
                    av_frame->data[0], 0, [](void*, uint8_t*) {}, nullptr, 0);
                av_frame->pts            = 0;
                avvk_color_frames[i][eye].frame = av_frame;

                if (use_depth) {
                    auto vk_depth_frame = av_vk_frame_alloc();
                    if (!vk_depth_frame) {
                        throw std::runtime_error{"Failed to allocate FFmpeg Vulkan frame"};
                    }
                    // The image index is just 0 here for AVVKFrame since we're not using multi-plane
                    vk_depth_frame->img[0]          = buffer_pool->depth_image_pool[i][eye].image;
                    vk_depth_frame->tiling          = buffer_pool->depth_image_pool[i][eye].image_info.tiling;
                    vk_depth_frame->mem[0]          = buffer_pool->depth_image_pool[i][eye].allocation_info.deviceMemory;
                    vk_depth_frame->size[0]         = buffer_pool->depth_image_pool[i][eye].allocation_info.size;
                    vk_depth_frame->offset[0]       = buffer_pool->depth_image_pool[i][eye].allocation_info.offset;
                    vk_depth_frame->queue_family[0] = dp->queues[vulkan::queue::GRAPHICS].family;

                    // VkExportSemaphoreCreateInfo export_semaphore_create_info{
                    //     VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO, nullptr, VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT};
                    vk_depth_frame->sem[0]       = vulkan::create_timeline_semaphore(dp->vk_device, 0, &export_semaphore_create_info);
                    vk_depth_frame->sem_value[0] = 0;

                    avvk_depth_frames[i][eye].vk_frame = vk_depth_frame;

                    // Create AVFrame
                    auto av_depth_frame = av_frame_alloc();
                    if (!av_depth_frame) {
                        throw std::runtime_error{"Failed to allocate FFmpeg frame"};
                    }
                    av_depth_frame->format        = AV_PIX_FMT_VULKAN;
                    av_depth_frame->width         = buffer_pool->depth_image_pool[i][eye].image_info.extent.width;
                    av_depth_frame->height        = buffer_pool->depth_image_pool[i][eye].image_info.extent.height;
                    av_depth_frame->hw_frames_ctx = av_buffer_ref(frame_ctx);
                    av_depth_frame->data[0]       = reinterpret_cast<uint8_t*>(vk_depth_frame);
                    av_depth_frame->buf[0]        = av_buffer_create(
                        av_depth_frame->data[0], 0, [](void*, uint8_t*) {}, nullptr, 0);
                    av_depth_frame->pts            = 0;
                    avvk_depth_frames[i][eye].frame = av_depth_frame;
                }
            }
        }

        for (size_t eye = 0; eye < 2; eye++) {
            decode_out_color_frames[eye]                = av_frame_alloc();
            decode_out_color_frames[eye]->format        = AV_PIX_FMT_CUDA;
            decode_out_color_frames[eye]->hw_frames_ctx = av_buffer_ref(cuda_nv12_frame_ctx);
            decode_out_color_frames[eye]->width         = buffer_pool->image_pool[0][0].image_info.extent.width;
            decode_out_color_frames[eye]->height        = buffer_pool->image_pool[0][0].image_info.extent.height;
            auto ret                              = av_hwframe_get_buffer(cuda_nv12_frame_ctx, decode_out_color_frames[eye], 0);
            AV_ASSERT_SUCCESS(ret);

            decode_converted_color_frames[eye]                = av_frame_alloc();
            decode_converted_color_frames[eye]->format        = AV_PIX_FMT_CUDA;
            decode_converted_color_frames[eye]->hw_frames_ctx = av_buffer_ref(cuda_bgra_frame_ctx);
            decode_converted_color_frames[eye]->width         = buffer_pool->image_pool[0][0].image_info.extent.width;
            decode_converted_color_frames[eye]->height        = buffer_pool->image_pool[0][0].image_info.extent.height;
            ret = av_hwframe_get_buffer(cuda_bgra_frame_ctx, decode_converted_color_frames[eye], 0);
            AV_ASSERT_SUCCESS(ret);

            if (use_depth) {
                decode_out_depth_frames[eye]                = av_frame_alloc();
                decode_out_depth_frames[eye]->format        = AV_PIX_FMT_CUDA;
                decode_out_depth_frames[eye]->hw_frames_ctx = av_buffer_ref(cuda_nv12_frame_ctx);
                decode_out_depth_frames[eye]->width         = buffer_pool->depth_image_pool[0][0].image_info.extent.width;
                decode_out_depth_frames[eye]->height        = buffer_pool->depth_image_pool[0][0].image_info.extent.height;
                decode_out_depth_frames[eye]->color_range   = AVCOL_RANGE_JPEG;
                ret                              = av_hwframe_get_buffer(cuda_nv12_frame_ctx, decode_out_depth_frames[eye], 0);
                AV_ASSERT_SUCCESS(ret);

                decode_converted_depth_frames[eye]                = av_frame_alloc();
                decode_converted_depth_frames[eye]->format        = AV_PIX_FMT_CUDA;
                decode_converted_depth_frames[eye]->hw_frames_ctx = av_buffer_ref(cuda_bgra_frame_ctx);
                decode_converted_depth_frames[eye]->width         = buffer_pool->depth_image_pool[0][0].image_info.extent.width;
                decode_converted_depth_frames[eye]->height        = buffer_pool->depth_image_pool[0][0].image_info.extent.height;
                ret = av_hwframe_get_buffer(cuda_bgra_frame_ctx, decode_converted_depth_frames[eye], 0);
                AV_ASSERT_SUCCESS(ret);
            }
        }

        yuv420_u_plane = nppiMalloc_8u_C1(buffer_pool->image_pool[0][0].image_info.extent.width / 2,
                                          buffer_pool->image_pool[0][0].image_info.extent.height / 2, &u_step);
        yuv420_v_plane = nppiMalloc_8u_C1(buffer_pool->image_pool[0][0].image_info.extent.width / 2,
                                          buffer_pool->image_pool[0][0].image_info.extent.height / 2, &v_step);
        yuv420_y_plane = nppiMalloc_8u_C1(buffer_pool->image_pool[0][0].image_info.extent.width,
                                          buffer_pool->image_pool[0][0].image_info.extent.height, &y_step);
    }

    void ffmpeg_init_decoder() {
        auto decoder = avcodec_find_decoder_by_name(OFFLOAD_RENDERING_FFMPEG_DECODER_NAME);
        if (!decoder) {
            throw std::runtime_error{"Failed to find FFmpeg decoder"};
        }

        this->codec_color_ctx = avcodec_alloc_context3(decoder);
        if (!codec_color_ctx) {
            throw std::runtime_error{"Failed to allocate FFmpeg decoder context"};
        }

        codec_color_ctx->thread_count = 0; // auto
        codec_color_ctx->thread_type  = FF_THREAD_SLICE;

        codec_color_ctx->pix_fmt       = AV_PIX_FMT_CUDA; // alpha channel will be useful later for compositing
        codec_color_ctx->sw_pix_fmt    = AV_PIX_FMT_NV12;
        codec_color_ctx->hw_device_ctx = av_buffer_ref(cuda_device_ctx);
        codec_color_ctx->hw_frames_ctx = av_buffer_ref(cuda_nv12_frame_ctx);
        codec_color_ctx->width         = buffer_pool->image_pool[0][0].image_info.extent.width;
        codec_color_ctx->height        = buffer_pool->image_pool[0][0].image_info.extent.height;
        codec_color_ctx->framerate     = {0, 1};
        codec_color_ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
        // codec_ctx->flags2 |= AV_CODEC_FLAG2_CHUNKS;

        // Set zero latency
        av_opt_set_int(codec_color_ctx->priv_data, "zerolatency", 1, 0);
        av_opt_set_int(codec_color_ctx->priv_data, "delay", 0, 0);

        av_opt_set(codec_color_ctx->priv_data, "hwaccel", "cuda", 0);

        // Set decoder profile
        // av_opt_set(codec_ctx->priv_data, "profile", "high", 0);

        // Set decoder preset
        // av_opt_set(codec_ctx->priv_data, "preset", "fast", 0);

        auto ret = avcodec_open2(codec_color_ctx, decoder, nullptr);
        AV_ASSERT_SUCCESS(ret);

        if (use_depth) {
            this->codec_depth_ctx = avcodec_alloc_context3(decoder);
            if (!codec_depth_ctx) {
                throw std::runtime_error{"Failed to allocate FFmpeg decoder context"};
            }

            codec_depth_ctx->thread_count = 0; // auto
            codec_depth_ctx->thread_type  = FF_THREAD_SLICE;

            codec_depth_ctx->pix_fmt       = AV_PIX_FMT_CUDA; // alpha channel will be useful later for compositing
            codec_depth_ctx->sw_pix_fmt    = AV_PIX_FMT_NV12;
            codec_depth_ctx->hw_device_ctx = av_buffer_ref(cuda_device_ctx);
            codec_depth_ctx->hw_frames_ctx = av_buffer_ref(cuda_nv12_frame_ctx);
            codec_depth_ctx->width         = buffer_pool->image_pool[0][0].image_info.extent.width;
            codec_depth_ctx->height        = buffer_pool->image_pool[0][0].image_info.extent.height;
            codec_depth_ctx->framerate     = {0, 1};
            codec_depth_ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
            // codec_ctx->flags2 |= AV_CODEC_FLAG2_CHUNKS;

            // Set zero latency
            av_opt_set_int(codec_depth_ctx->priv_data, "zerolatency", 1, 0);
            av_opt_set_int(codec_depth_ctx->priv_data, "delay", 0, 0);

            av_opt_set(codec_depth_ctx->priv_data, "hwaccel", "cuda", 0);

            // Set decoder profile
            // av_opt_set(codec_ctx->priv_data, "profile", "high", 0);

            // Set decoder preset
            // av_opt_set(codec_ctx->priv_data, "preset", "fast", 0);

            auto ret = avcodec_open2(codec_depth_ctx, decoder, nullptr);
            AV_ASSERT_SUCCESS(ret);
        }
    }

    std::shared_ptr<RelativeClock> clock;
};

class offload_rendering_client_loader
    : public plugin
    , public vulkan::vk_extension_request {
public:
    offload_rendering_client_loader(const std::string& name, phonebook* pb)
        : plugin(name, pb)
        , offload_rendering_client_plugin{std::make_shared<offload_rendering_client>(name, pb)} {
        pb->register_impl<vulkan::app>(offload_rendering_client_plugin);
        std::cout << "Registered vulkan::timewarp" << std::endl;
    }

    std::vector<const char*> get_required_instance_extensions() override {
        return {VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME, VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME,
                VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME};
    }

    std::vector<const char*> get_required_devices_extensions() override {
        std::vector<const char*> device_extensions;
        device_extensions.push_back(VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME);
        device_extensions.push_back(VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME);
        device_extensions.push_back(VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);
        device_extensions.push_back(VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME);
        device_extensions.push_back(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
        return device_extensions;
    }

    void start() override {
        offload_rendering_client_plugin->start();
    }

    void stop() override {
        offload_rendering_client_plugin->stop();
    }

private:
    std::shared_ptr<offload_rendering_client> offload_rendering_client_plugin;
};

PLUGIN_MAIN(offload_rendering_client_loader)