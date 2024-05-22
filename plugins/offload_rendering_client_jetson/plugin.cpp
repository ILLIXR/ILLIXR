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

#include "illixr/pose_prediction.hpp"
#include "decoding/video_decode.h"

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

        compare_images = std::getenv("ILLIXR_COMPARE_IMAGES") != nullptr && std::stoi(std::getenv("ILLIXR_COMPARE_IMAGES"));
        if (compare_images) {
            log->debug("Sending constant pose to compare images");
            // Constant pose as recorded from the GT. Note that the Quaternion constructor takes the w component first.

            // 0 ms
            // Timepoint: 25205 ms; Pose Position: -0.891115 0.732361 -0.536178; Pose Orientiation: 0.0519684 -0.113465 0.0679164 0.989855
//            fixed_pose = pose_type(time_point(), Eigen::Vector3f(-0.891115, 0.732361, -0.536178),
//                                   Eigen::Quaternionf(0.989855, 0.0519684, -0.113465, 0.0679164));

            // -25 ms
            // Timepoint: 25177 ms; Pose Position: -0.897286 0.732254 -0.533516; Pose Orientiation: 0.050736 -0.112192 0.0657815 0.990208
//            fixed_pose = pose_type(time_point(), Eigen::Vector3f(-0.897286, 0.732254, -0.533516),
//                                   Eigen::Quaternionf(0.990208, 0.050736, -0.112192, 0.0657815));

            // -50 ms
            // Timepoint: 25156 ms; Pose Position: -0.901611 0.732027 -0.531002; Pose Orientiation: 0.0499585 -0.110069 0.064728 0.990555
//            fixed_pose = pose_type(time_point(), Eigen::Vector3f(-0.901611, 0.732027, -0.531002),
//                                   Eigen::Quaternionf(0.990555, 0.0499585, -0.110069, 0.064728));

            // -75 ms
            // Timepoint: 25128 ms; Pose Position: -0.90597 0.731928 -0.527296; Pose Orientiation: 0.0492738 -0.105977 0.0637901 0.991096
//            fixed_pose = pose_type(time_point(), Eigen::Vector3f(-0.90597, 0.731928, -0.527296),
//                                   Eigen::Quaternionf(0.991096, 0.0492738, -0.105977, 0.0637901));

            // -100 ms
            // Timepoint: 25103 ms; Pose Position: -0.906131 0.732548 -0.526771; Pose Orientiation: 0.0486073 -0.10057 0.0633693 0.991719
//            fixed_pose = pose_type(time_point(), Eigen::Vector3f(-0.906131, 0.732548, -0.526771),
//                                   Eigen::Quaternionf(0.991719, 0.0486073, -0.10057, 0.0633693));

            // -125 ms
            // Timepoint: 25080 ms; Pose Position: -0.906863 0.732095 -0.527118; Pose Orientiation: 0.0478971 -0.0949276 0.0636839 0.99229
//            fixed_pose = pose_type(time_point(), Eigen::Vector3f(-0.906863, 0.732095, -0.527118),
//                                   Eigen::Quaternionf(0.99229, 0.0478971, -0.0949276, 0.0636839));

            // -150 ms
            // Timepoint: 25052 ms; Pose Position: -0.907584 0.731722 -0.527914; Pose Orientiation: 0.0473371 -0.0867409 0.0639834 0.993047
//            fixed_pose = pose_type(time_point(), Eigen::Vector3f(-0.907584, 0.731722, -0.527914),
//                                   Eigen::Quaternionf(0.993047, 0.0473371, -0.0867409, 0.0639834));

            // -175 ms
            // Timepoint: 25032 ms; Pose Position: -0.909664 0.73097 -0.527997; Pose Orientiation: 0.0470376 -0.0787137 0.0641232 0.99372
//            fixed_pose = pose_type(time_point(), Eigen::Vector3f(-0.909664, 0.73097, -0.527997),
//                                   Eigen::Quaternionf(0.99372, 0.0470376, -0.0787137, 0.0641232));

            // -200 ms
            // Timepoint: 25003 ms; Pose Position: -0.912881 0.729179 -0.527451; Pose Orientiation: 0.0463598 -0.0647321 0.0649133 0.994709
            fixed_pose = pose_type(time_point(), Eigen::Vector3f(-0.912881, 0.729179, -0.527451),
                                   Eigen::Quaternionf(0.994709, 0.0463598, -0.0647321, 0.0649133));
        } else {
            log->debug("Cliont sending normal poses (not comparing images)");
        }
    }

    void start() override {
        mmapi_init_decoders();
        threadloop::start();
    }

    void mmapi_init_decoders() {
        for (auto eye = 0; eye < 2; eye++) {
            rgb_decoders[eye].decoder_init();
            if (use_depth) {
                depth_decoders[eye].decoder_init();
            }
        }
    }

    virtual void setup(VkRenderPass render_pass, uint32_t subpass,
                       std::shared_ptr<vulkan::buffer_pool<fast_pose_type>> buffer_pool) override {
        this->buffer_pool = buffer_pool;
        command_pool      = vulkan::create_command_pool(dp->vk_device, dp->queues[vulkan::queue::GRAPHICS].family);
        ready = true;

        for (auto& frame : buffer_pool->image_pool) {
            for (auto& eye : frame) {
                auto cmd_buf = vulkan::begin_one_time_command(dp->vk_device, command_pool);
                transition_layout(cmd_buf, eye.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                vulkan::end_one_time_command(dp->vk_device, command_pool, dp->queues[vulkan::queue::GRAPHICS], cmd_buf);
            }
        }

        if (use_depth) {
            for (auto& frame : buffer_pool->depth_image_pool) {
                for (auto& eye : frame) {
                    auto cmd_buf = vulkan::begin_one_time_command(dp->vk_device, command_pool);
                    transition_layout(cmd_buf, eye.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                    vulkan::end_one_time_command(dp->vk_device, command_pool, dp->queues[vulkan::queue::GRAPHICS], cmd_buf);
                }
            }
        }

        for (size_t i = 0; i < buffer_pool->image_pool; i++) {
            for (auto eye = 0; eye < 2; eye++) {
                layout_transition_start_cmd_bufs[i][eye] = vulkan::create_command_buffer(dp->vk_device, command_pool);
                VkCommandBufferBeginInfo begin_info{};
                begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                begin_info.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
                vkBeginCommandBuffer(layout_transition_start_cmd_bufs[i][eye], &begin_info);
                transition_layout(layout_transition_start_cmd_bufs[i][eye], buffer_pool->image_pool[i][eye].image,
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
                if (use_depth) {
                    transition_layout(layout_transition_start_cmd_bufs[i][eye], buffer_pool->depth_image_pool[i][eye].image,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
                }
                vkEndCommandBuffer(layout_transition_start_cmd_bufs[i][eye]);

                layout_transition_end_cmd_bufs[i][eye] = vulkan::create_command_buffer(dp->vk_device, command_pool);
                vkBeginCommandBuffer(layout_transition_end_cmd_bufs[i][eye], &begin_info);
                transition_layout(layout_transition_end_cmd_bufs[i][eye], buffer_pool->image_pool[i][eye].image,
                                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                if (use_depth) {
                    transition_layout(layout_transition_end_cmd_bufs[i][eye], buffer_pool->depth_image_pool[i][eye].image,
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
        for (auto eye = 0; eye < 2; eye++) {
            rgb_decoders[eye].decoder_destroy();
            if (use_depth) {
                depth_decoders[eye].decoder_destroy();
            }
        }
    }

protected:
    void _p_thread_setup() override { }

    skip_option _p_should_skip() override {
        return threadloop::_p_should_skip();
    }

    // Vulkan layout transition
    // supports: shader read <-> transfer dst
    void transition_layout(VkCommandBuffer cmd_buf, VkImage image, VkImageLayout old_layout, VkImageLayout new_layout) {
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
            decode_src_color_packets[eye]->data;
            auto ret = avcodec_send_packet(codec_color_ctx, );
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
    bool compare_images = false;
    pose_type fixed_pose;

    std::vector<std::array<VkCommandBuffer, 2>>          layout_transition_start_cmd_bufs;
    std::vector<std::array<VkCommandBuffer, 2>>          layout_transition_end_cmd_bufs;

    std::array<mmapi_decoder, 2> rgb_decoders;
    std::array<mmapi_decoder, 2> depth_decoders;

    fast_pose_type decoded_frame_pose;

    VkCommandPool command_pool{};

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
        // Send an identity pose if comparing images
        auto current_pose = pp->get_fast_pose();
        if (compare_images) {
            current_pose.pose = fixed_pose;
        }

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