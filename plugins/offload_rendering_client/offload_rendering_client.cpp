#include "offload_rendering_client.hpp"

#ifdef USING_OPENXR
#    include "illixr/data_format/poses/combined_pose.hpp"
#endif
#ifdef __ANDROID__
#    include "android/jni_helper.hpp"
#else
#    include <cuda.h>
#    include <cuda_runtime.h>
#    include <nppi_color_conversion.h>
#    define OFFLOAD_RENDERING_FFMPEG_DECODER_NAME "hevc"
#endif

using namespace ILLIXR;
using namespace ILLIXR::data_format;
#ifdef __ANDROID__
static bool is_hevc_keyframe(const uint8_t* data, size_t size) {
    // Need at least 6 bytes: 4-byte start code + 2-byte NAL header
    if (size < 6)
        return false;

    // Skip the start code (00 00 00 01)
    const uint8_t* nal = data + 4;

    // NAL unit type = bits [9:15] of the 2-byte header
    // = (first_byte & 0x7E) >> 1
    uint8_t nal_type = (nal[0] & 0x7E) >> 1;

    // IDR_W_RADL = 19, IDR_N_LP = 20
    return nal_type == 19 || nal_type == 20;
}

constexpr int I_HEADSET_WIDTH  = static_cast<int>(HEADSET_WIDTH * 1.1);
constexpr int I_HEADSET_HEIGHT = static_cast<int>(HEADSET_HEIGHT * 1.1);

#else
using namespace ILLIXR::vulkan::ffmpeg_utils;

NppStreamContext makeNppStreamContext(cudaStream_t stream = nullptr) {
    NppStreamContext ctx{};
    int              device = 0;
    cudaGetDevice(&device);
    cudaDeviceProp prop{};
    cudaGetDeviceProperties(&prop, device);

    ctx.hStream                            = stream;
    ctx.nCudaDeviceId                      = device;
    ctx.nMultiProcessorCount               = prop.multiProcessorCount;
    ctx.nMaxThreadsPerMultiProcessor       = prop.maxThreadsPerMultiProcessor;
    ctx.nMaxThreadsPerBlock                = prop.maxThreadsPerBlock;
    ctx.nSharedMemPerBlock                 = prop.sharedMemPerBlock;
    ctx.nCudaDevAttrComputeCapabilityMajor = prop.major;
    ctx.nCudaDevAttrComputeCapabilityMinor = prop.minor;
    return ctx;
}
#endif

const int log_interval = 300; // pose logging interval in frames

offload_rendering_client::offload_rendering_client(const std::string& name, phonebook* pb)
    : threadloop{name, pb}
    , switchboard_{pb->lookup_impl<switchboard>()}
    , log_{spdlogger(nullptr)}
#ifdef __ANDROID__
    , frame_writer_{switchboard_->get_writer<data_format::dual_frames>("unity_rendered_frame")}
#else
    , display_provider_{pb->lookup_impl<vulkan::display_provider>()}
#endif
    , frames_reader_{switchboard_->get_buffered_reader<compressed_frame>("compressed_frames")}
    , network_latency_reader_{switchboard_->get_reader<network_latency_result>("network_latency")}
#ifndef USING_OPENXR
    , pose_writer_{switchboard_->get_network_writer<pose::fast_head_pose_type>("render_pose", {})}
#endif
#ifndef USING_OPENXR
    , pose_prediction_{pb->lookup_impl<pose_prediction>()}
#endif
    , clock_{pb->lookup_impl<relative_clock>()}
#ifdef __ANDROID__
    , app_{switchboard_->get_android_app()} {
#else
{
    display_provider_ffmpeg = display_provider_;
#endif

#ifdef __ANDROID__
    // Motion vectors are decoded through the Android MediaCodec path and also
    // require the depth-image path to be active.
    use_motion_vectors_ = switchboard_->get_env_bool("ILLIXR_USE_MOTION_VECTORS");
    if (use_motion_vectors_) {
        use_depth_ = true;
    } else {
        use_depth_ = switchboard_->get_env_bool("ILLIXR_USE_DEPTH_IMAGES");
    }
    log_->debug(use_motion_vectors_ ? "Encoding motion vector images for the client"
                                    : "Not encoding motion vector images for the client");
#else
    use_depth_ = switchboard_->get_env_bool("ILLIXR_USE_DEPTH_IMAGES");
#endif
    log_->debug(use_depth_ ? "Encoding depth images for the client" : "Not encoding depth images for the client");
}

#ifdef __ANDROID__
offload_rendering_client::~offload_rendering_client() {
    spdlog::get("illixr")->info("[offload_rendering_client] Shutting down, processed {} frames", frame_count_);

    // Stop the receiver thread before stopping decoders so it cannot submit
    // new encoded data after the decoders have been torn down.
    receiver_running_ = false;
    if (receiver_thread_.joinable()) {
        receiver_thread_.join();
    }
    // Log final timing statistics
    if (color_decoder_) {
        auto color_stats = color_decoder_->get_timing_stats();
        log_->info("[offload_rendering_client] Final color decode stats: "
                   "left_avg={:.2f}ms, right_avg={:.2f}ms, total_frames={}",
                   color_stats.left_eye.avg_decode_time_us() / 1000.0, color_stats.right_eye.avg_decode_time_us() / 1000.0,
                   color_stats.total_frames());
        color_decoder_->stop();
    }
    if (depth_decoder_) {
        auto depth_stats = depth_decoder_->get_timing_stats();
        log_->info("[offload_rendering_client] Final depth decode stats: "
                   "left_avg={:.2f}ms, right_avg={:.2f}ms, total_frames={}",
                   depth_stats.left_eye.avg_decode_time_us() / 1000.0, depth_stats.right_eye.avg_decode_time_us() / 1000.0,
                   depth_stats.total_frames());
        depth_decoder_->stop();
    }
    if (motion_vec_decoder_) {
        auto mv_stats = motion_vec_decoder_->get_timing_stats();
        log_->info("[offload_rendering_client] Final MV decode stats: "
                   "left_avg={:.2f}ms, right_avg={:.2f}ms, total_frames={}",
                   mv_stats.left_eye.avg_decode_time_us() / 1000.0, mv_stats.right_eye.avg_decode_time_us() / 1000.0,
                   mv_stats.total_frames());
        motion_vec_decoder_->stop();
    }
}

void offload_rendering_client::log_android_decode_timing() {
    // Calculate averages from accumulated metrics
    if (frame_count_ == 0) {
        return;
    }

    // Get decoder statistics
    stereo_decode_timing_stats color_stats;
    stereo_decode_timing_stats depth_stats;
    stereo_decode_timing_stats mv_stats;

    // Color decoder stats
    if (color_decoder_) {
        color_stats = color_decoder_->get_and_reset_timing_stats();
        if (color_stats.left_eye.max_decode_latency_us > 0 || color_stats.right_eye.max_decode_latency_us > 0) {
            log_->info("  Color decode latency (left):  avg={:.2f}ms, min={:.2f}ms, max={:.2f}ms",
                       color_stats.left_eye.avg_decode_time_us() / 1000.0,
                       color_stats.left_eye.min_decode_latency_us == UINT64_MAX
                           ? 0
                           : color_stats.left_eye.min_decode_latency_us / 1000.0,
                       color_stats.left_eye.max_decode_latency_us / 1000.0);
#    ifndef COMBINED_ENCODING
            log_->info("  Color decode latency (right): avg={:.2f}ms, min={:.2f}ms, max={:.2f}ms",
                       color_stats.right_eye.avg_decode_time_us() / 1000.0,
                       color_stats.right_eye.min_decode_latency_us == UINT64_MAX
                           ? 0
                           : color_stats.right_eye.min_decode_latency_us / 1000.0,
                       color_stats.right_eye.max_decode_latency_us / 1000.0);
#    endif
        }
    }

    // Depth decoder stats
    if (use_depth_ && depth_decoder_) {
        depth_stats = depth_decoder_->get_and_reset_timing_stats();
        if (depth_stats.left_eye.avg_decode_time_us() > 0.) {
            log_->info("  Depth decode latency (left):  avg={:.2f}ms, min={:.2f}ms, max={:.2f}ms",
                       depth_stats.left_eye.avg_decode_time_us() / 1000.0,
                       depth_stats.left_eye.min_decode_latency_us == UINT64_MAX
                           ? 0
                           : depth_stats.left_eye.min_decode_latency_us / 1000.0,
                       depth_stats.left_eye.max_decode_latency_us / 1000.0);
        }
#    ifndef COMBINED_ENCODING
        if (depth_stats.right_eye.avg_decode_time_us() > 0.) {
            log_->info("  Depth decode latency (right): avg={:.2f}ms, min={:.2f}ms, max={:.2f}ms",
                       depth_stats.right_eye.avg_decode_time_us() / 1000.0,
                       depth_stats.right_eye.min_decode_latency_us == UINT64_MAX
                           ? 0
                           : depth_stats.right_eye.min_decode_latency_us / 1000.0,
                       depth_stats.right_eye.max_decode_latency_us / 1000.0);
        }
#    endif
    }

    if (use_motion_vectors_ && motion_vec_decoder_) {
        mv_stats = motion_vec_decoder_->get_and_reset_timing_stats();
        log_->info("  MV decode latency (left):  avg={:.2f}ms, min={:.2f}ms, max={:.2f}ms",
                   mv_stats.left_eye.avg_decode_time_us() / 1000.0,
                   mv_stats.left_eye.min_decode_latency_us == UINT64_MAX ? 0 : mv_stats.left_eye.min_decode_latency_us / 1000.0,
                   mv_stats.left_eye.max_decode_latency_us / 1000.0);
#    ifndef COMBINED_ENCODING
        log_->info("  MV decode latency (right): avg={:.2f}ms, min={:.2f}ms, max={:.2f}ms",
                   mv_stats.right_eye.avg_decode_time_us() / 1000.0,
                   mv_stats.right_eye.min_decode_latency_us == UINT64_MAX ? 0
                                                                          : mv_stats.right_eye.min_decode_latency_us / 1000.0,
                   mv_stats.right_eye.max_decode_latency_us / 1000.0);
#    endif
    }

    // log_->info("==========================================");

    // Reset accumulated metrics
    // android_texture_update_time_us_ = 0;
    // android_total_frame_time_us_ = 0;
    // android_timing_frame_count_ = 0;
}

// receiver_loop
// Runs on receiver_thread_. Dequeues compressed frames from the network,
// drops frames when the decoder input queue is full to prevent growing latency,
// queues encoded data to the hardware decoders (all three stream types together
// so they stay in sync), and stores frame metadata for _p_one_iteration.
// receiver_loop
// Runs on receiver_thread_. Dequeues compressed frames from the network,
// drops frames when the decoder input queue is full to prevent growing latency,
// queues encoded data to the hardware decoders (all three stream types together
// so they stay in sync), and stores frame metadata for _p_one_iteration.
void offload_rendering_client::receiver_loop() {
    // spdlog::get("illixr")->info("[receiver_loop] Starting");

    while (receiver_running_) {
        auto current_frame = frames_reader_.dequeue();
        if (current_frame == nullptr) {
            spdlog::get("illixr")->debug("[receiver_loop] No frame available");
            continue;
        }

        // spdlog::get("illixr")->info("Rx Frame {}", current_frame->frame_number);
        //  Determine keyframe status for each stream.
        //
        //  Color: use the authoritative flag set by the server from
        //  nvenc_encoder::last_frame_was_keyframe().  This works correctly for
        //  both HEVC (IDR NAL) and AV1 (KEY_FRAME OBU) without any bitstream
        //  parsing on the client side.
        //
        //  Depth / motion-vector streams are always HEVC regardless of USE_AV1,
        //  so they continue to use the NAL-unit scan.
        const bool is_key_color = current_frame->is_keyframe;

        const bool is_key_depth = (use_depth_ && !current_frame->left_depth.empty())
            ? is_hevc_keyframe(current_frame->left_depth.data(), current_frame->left_depth.size())
            : is_key_color;

        const bool is_key_mv = (use_motion_vectors_ && !current_frame->left_motion_vec.empty())
            ? is_hevc_keyframe(current_frame->left_motion_vec.data(), current_frame->left_motion_vec.size())
            : is_key_color;

        // Never drop a frame if any stream carries a keyframe - dropping a
        // keyframe causes decoder corruption until the next IDR arrives.
        const bool is_any_key = is_key_color || is_key_depth || is_key_mv;

        // Drop this frame if the color decoder input queue is already full
        // and no stream is a keyframe. This prevents latency from growing
        // unboundedly when the hardware decoder cannot keep up.
        if (!is_any_key && color_decoder_) {
            const size_t left_depth  = color_decoder_->get_left_queue_depth();
            const size_t right_depth = color_decoder_->get_right_queue_depth();
            if (left_depth >= MAX_DECODER_QUEUE_DEPTH || right_depth >= MAX_DECODER_QUEUE_DEPTH) {
                spdlog::get("illixr")->warn("[receiver_loop] Dropping P-frame {} - decoder queue full "
                                            "(left={}, right={})",
                                            current_frame->frame_number, left_depth, right_depth);
                continue;
            }
        }

        // Queue encoded data to the hardware decoders.
        // All stream types are submitted together so they stay in sync -
        // if we drop above, we drop all three atomically.
#    ifdef COMBINED_ENCODING
        // Combined mode: left_color holds the full side-by-side bitstream;
        // right_color is empty on the wire.  Feed only eye=0.
        if (color_decoder_ && !current_frame->left_color.empty()) {
            color_decoder_->queue_encoded_data(0, current_frame->left_color.data(), current_frame->left_color.size(),
                                               current_frame->sent_time, is_key_color, current_frame->frame_number);
        }

        // Depth and motion-vector streams remain per-eye even under
        // COMBINED_ENCODING (server sends them as separate streams).
        for (int eye = 0; eye < 2; eye++) {
            if (use_depth_ && depth_decoder_) {
                const auto& depth_pkt = (eye == 0) ? current_frame->left_depth : current_frame->right_depth;
                if (!depth_pkt.empty()) {
                    depth_decoder_->queue_encoded_data(eye, depth_pkt.data(), depth_pkt.size(), current_frame->sent_time,
                                                       is_key_depth, current_frame->frame_number);
                }
            }

            if (use_motion_vectors_ && motion_vec_decoder_ && current_frame->use_motion_vectors) {
                const auto& mv_pkt = (eye == 0) ? current_frame->left_motion_vec : current_frame->right_motion_vec;
                if (!mv_pkt.empty()) {
                    motion_vec_decoder_->queue_encoded_data(eye, mv_pkt.data(), mv_pkt.size(), current_frame->sent_time,
                                                            is_key_mv, current_frame->frame_number);
                }
            }
        }
#    else
        for (int eye = 0; eye < 2; eye++) {
            const auto& color_pkt = (eye == 0) ? current_frame->left_color : current_frame->right_color;
            if (color_decoder_ && !color_pkt.empty()) {
                color_decoder_->queue_encoded_data(eye, color_pkt.data(), color_pkt.size(), current_frame->sent_time,
                                                   is_key_color, current_frame->frame_number);
            }

            if (use_depth_ && depth_decoder_) {
                const auto& depth_pkt = (eye == 0) ? current_frame->left_depth : current_frame->right_depth;
                if (!depth_pkt.empty()) {
                    depth_decoder_->queue_encoded_data(eye, depth_pkt.data(), depth_pkt.size(), current_frame->sent_time,
                                                       is_key_depth, current_frame->frame_number);
                }
            }

            if (use_motion_vectors_ && motion_vec_decoder_ && current_frame->use_motion_vectors) {
                const auto& mv_pkt = (eye == 0) ? current_frame->left_motion_vec : current_frame->right_motion_vec;
                if (!mv_pkt.empty()) {
                    motion_vec_decoder_->queue_encoded_data(eye, mv_pkt.data(), mv_pkt.size(), current_frame->sent_time,
                                                            is_key_mv, current_frame->frame_number);
                }
            }
        }
#    endif // COMBINED_ENCODING

        // Store metadata so _p_one_iteration can populate dual_frames.
        // The mutex ensures _p_one_iteration always sees a consistent snapshot.
        {
            std::lock_guard<std::mutex> lock(frame_meta_map_mutex_);
            frame_meta&                 meta = frame_meta_map_[current_frame->frame_number];
            meta.pose                        = current_frame->pose;
            meta.frame_number                = current_frame->frame_number;
            meta.frame_time                  = current_frame->sent_time;
            meta.pose_id                     = current_frame->pose_id;
            meta.near_z                      = current_frame->near_z;
            meta.far_z                       = current_frame->far_z;
            meta.encode_time                 = current_frame->encode_time;

            // Cache first non-zero FOV received from server
            if (!fov_cached_ && current_frame->fov_left[0] != 0.0f) {
                cached_fov_left_  = current_frame->fov_left;
                cached_fov_right_ = current_frame->fov_right;
                cached_fov_up_    = current_frame->fov_up;
                cached_fov_down_  = current_frame->fov_down;
                fov_cached_       = true;
            }
        }
    }
}
#else

void offload_rendering_client::start() {
    ffmpeg_init_device();
    ffmpeg_init_cuda_device();
    threadloop::start();
}

void offload_rendering_client::setup(VkRenderPass render_pass, uint32_t subpass,
                                     std::shared_ptr<vulkan::buffer_pool<pose::fast_head_pose_type>> buffer_pool) {
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

#endif // __ANDROID__

void offload_rendering_client::_p_thread_setup() {
#ifdef __ANDROID__
    spdlog::get("illixr")->info("[android_media_decoder] Thread setup starting");

    // Initialize color decoder - no GL/EGL arguments in the Vulkan path.
    color_decoder_ = std::make_unique<stereo_surface_decoder>(I_HEADSET_WIDTH, I_HEADSET_HEIGHT, false);
    if (!color_decoder_->initialize()) {
        spdlog::get("illixr")->error("[android_media_decoder] Failed to initialize color decoder (Vulkan path)");
        color_decoder_.reset();
        return;
    }

    if (use_motion_vectors_) {
        // Motion vectors are 432x432 HEVC 10-bit.  The frame_decoder uses
        // "video/hevc" which handles both 8-bit and 10-bit bitstreams.
        // AIMAGE_FORMAT_PRIVATE allows the Snapdragon XR2 Gen 2 hardware
        // decoder to output directly to GPU memory (no CPU copy).
        // depth_decoder_ must have the same dimensions as the motion_vec_decoder_
        depth_decoder_ = std::make_unique<stereo_surface_decoder>(MOTION_VEC_WIDTH, MOTION_VEC_HEIGHT, true);
        if (!depth_decoder_->initialize()) {
            spdlog::get("illixr")->warn("[android_media_decoder] Failed to initialize depth decoder (Vulkan path), "
                                        "depth will be unavailable");
            depth_decoder_.reset();
        }

        motion_vec_decoder_ = std::make_unique<stereo_surface_decoder>(MOTION_VEC_WIDTH, MOTION_VEC_HEIGHT, false, true);
        if (!motion_vec_decoder_->initialize()) {
            spdlog::get("illixr")->warn("[android_media_decoder] Failed to initialize motion-vector decoder "
                                        "(Vulkan path); App Spacewarp will be unavailable");
            motion_vec_decoder_.reset();
        } else {
            spdlog::get("illixr")->info("[android_media_decoder] Motion-vector decoder initialized ({}x{})", MOTION_VEC_WIDTH,
                                        MOTION_VEC_HEIGHT);
        }
    } else if (use_depth_) {
        depth_decoder_ = std::make_unique<stereo_surface_decoder>(I_HEADSET_WIDTH, I_HEADSET_HEIGHT, true);
        if (!depth_decoder_->initialize()) {
            spdlog::get("illixr")->warn("[android_media_decoder] Failed to initialize depth decoder (Vulkan path), "
                                        "depth will be unavailable");
            depth_decoder_.reset();
        }
    }

    ready_ = true;
    // Launch the receiver thread now that all decoders are initialized.
    // It feeds encoded data to the decoders independently of _p_one_iteration,
    // preventing decoder pipeline latency from blocking network reception.
    receiver_running_ = true;
    receiver_thread_  = std::thread([this]() {
        receiver_loop();
    });

    spdlog::get("illixr")->info("[android_media_decoder] Decoder initialized, receiver thread started");
#endif // __ANDROID__
}

void offload_rendering_client::_p_one_iteration() {
#ifdef __ANDROID__
    if (!ready_ || !color_decoder_ || !color_decoder_->is_ready()) {
#else
    if (!ready_) {
#endif // __ANDROID__
        return;
    }
    // Send latest pose to server
#ifndef USING_OPENXR
    push_pose();
#endif

#ifdef __ANDROID__
    // Consumer phase
    // The receiver_thread_ feeds encoded data to the decoders asynchronously.
    // Here we just acquire whatever the decoders have most recently finished
    // and publish it to the switchboard.

    // ============ TEXTURE UPDATE PHASE ============
    // Get current color frame with textures and transforms
    // This includes calling updateTexImage() which synchronizes with the decoder
    // auto texture_update_start = std::chrono::high_resolution_clock::now();

    // Get complete frame with both color and depth
    dual_frames frame = construct_dual_frames(clock_->now());

    // auto texture_update_end = std::chrono::high_resolution_clock::now();

    // android_texture_update_time_us_ += static_cast<uint64_t>(
    //     std::chrono::duration_cast<std::chrono::microseconds>(
    //         texture_update_end - texture_update_start).count());

    if (frame.is_valid()) {
        last_submitted_frame_ = frame.frame_number;
        frame_writer_.put(frame_writer_.allocate(std::move(frame)));
        frame_count_++;
    } else {
        std::this_thread::sleep_for(std::chrono::microseconds(500));
    }
    // auto frame_end = std::chrono::high_resolution_clock::now();
    // android_total_frame_time_us_ += static_cast<uint64_t>(
    //     std::chrono::duration_cast<std::chrono::microseconds>(
    //         frame_end - frame_start).count());
    // android_timing_frame_count_++;

#else

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

        auto ret =
            nppiNV12ToYUV420_8u_P2P3R_Ctx(pSrc, decode_out_color_frames_[eye]->linesize[0], pDst, dst_linesizes, roi, npp_ctx_);
        assert(ret == NPP_SUCCESS);

        // Convert YUV420 to BGRA
        auto dst = reinterpret_cast<Npp8u*>(decode_converted_color_frames_[eye]->data[0]);
        ret      = nppiYUV420ToBGR_8u_P3C4R_Ctx(pDst, dst_linesizes, dst, decode_converted_color_frames_[eye]->linesize[0], roi,
                                                npp_ctx_);
        assert(ret == NPP_SUCCESS);

        // Process depth frames if enabled
        if (use_depth_) {
            // NppiSize roi_depth = {static_cast<int>(decode_out_depth_frames_[eye]->width),
            //                       static_cast<int>(decode_out_depth_frames_[eye]->height)};
            Npp8u* pSrc_depth[2];
            pSrc_depth[0] = reinterpret_cast<Npp8u*>(decode_out_depth_frames_[eye]->data[0]);
            pSrc_depth[1] = reinterpret_cast<Npp8u*>(decode_out_depth_frames_[eye]->data[1]);
            ret = nppiNV12ToYUV420_8u_P2P3R_Ctx(pSrc_depth, decode_out_depth_frames_[eye]->linesize[0], pDst, dst_linesizes,
                                                roi, npp_ctx_);
            assert(ret == NPP_SUCCESS);
            auto dst_depth = reinterpret_cast<Npp8u*>(decode_converted_depth_frames_[eye]->data[0]);
            ret = nppiYUV420ToBGR_8u_P3C4R_Ctx(pDst, dst_linesizes, dst_depth, decode_converted_depth_frames_[eye]->linesize[0],
                                               roi, npp_ctx_);
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
#endif

    // Log performance metrics every second
    if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - fps_start_time_).count() >=
        1) {
        log_->info("Decoder FPS: {}", fps_counter_);
        fps_start_time_ = std::chrono::high_resolution_clock::now();

        for (auto& metric : metrics_) {
            auto fps = std::max(fps_counter_, (uint16_t) 1);
            log_->info("{}: {}", metric.first, metric.second / (double) (fps));
            metric.second = 0;
        }
#ifdef __ANDROID__
        // Log detailed Android timing statistics
        log_android_decode_timing();
#endif
        fps_counter_ = 1;
    } else {
        fps_counter_++;
    }
}

#ifndef USING_OPENXR
void offload_rendering_client::push_pose() {
    auto current_pose = pose_prediction_->get_fast_pose();

    auto now = time_point{std::chrono::duration<long, std::nano>{std::chrono::high_resolution_clock::now().time_since_epoch()}};
    current_pose.predict_target_time   = now;
    current_pose.predict_computed_time = now;
    pose_writer_.put(std::make_shared<pose::fast_head_pose_type>(current_pose));
}
#endif

#ifdef __ANDROID__
// construct_dual_frames now takes frame_meta by const-ref instead of reading
// stale member variables, so receiver_thread_ and _p_one_iteration never race
// on the same pose/frame_number/etc. fields.
data_format::dual_frames offload_rendering_client::construct_dual_frames(time_point render_time) {
    // Vulkan path: acquire AHardwareBuffers from both decoders
    dual_frames frame = color_decoder_->get_current_frame(render_time);
    frame_meta  meta;

    if (frame.is_valid()) {
        // frame.frame_number was set atomically with the buffer acquisition
        // inside acquire_latest_buffer() - no separate call needed.
        if (frame.frame_number <= last_submitted_frame_)
            return {};
        const uint64_t decoded_frame_number = frame.frame_number;
        {
            std::lock_guard<std::mutex> lock(frame_meta_map_mutex_);
            auto                        it = frame_meta_map_.find(decoded_frame_number);
            if (it != frame_meta_map_.end()) {
                meta = it->second;
                // Erase this entry and everything older - the map is ordered by
                // frame_number so begin()..next(it) covers all stale entries.
                auto it2 = frame_meta_map_.begin();
                while (it2 != frame_meta_map_.end()) {
                    if (it2->first < decoded_frame_number) {
                        it2 = frame_meta_map_.erase(it2);
                    } else {
                        ++it2;
                    }
                }
            } else if (!frame_meta_map_.empty()) {
                spdlog::get("illixr")->debug("Could not find meta for {}", decoded_frame_number);
                // Decoded frame not in map (dropped or not yet arrived).
                // Use the most recent available entry as the best approximation.
                meta = frame_meta_map_.rbegin()->second;
            } else if (it == frame_meta_map_.end()) {
                spdlog::get("illixr")->error("[openxr_interface] No meta for frame {}, dropping", decoded_frame_number);
                return {};
            } else {
                spdlog::get("illixr")->debug("meta map empty");
            }
            // If map is empty, meta stays default-constructed - frame will likely
            // fail is_valid() and be discarded by the caller.
        }
        if (meta.consumed) {
            return {}; // another thread already claimed this frame
        }
    }
    frame.pose        = meta.pose;
    frame.near_z      = meta.near_z;
    frame.far_z       = meta.far_z;
    frame.pose_id     = meta.pose_id;
    frame.encode_time = meta.encode_time;
    frame.fov_left    = cached_fov_left_;
    frame.fov_right   = cached_fov_right_;
    frame.fov_up      = cached_fov_up_;
    frame.fov_down    = cached_fov_down_;

    if (use_depth_ && depth_decoder_ && depth_decoder_->is_ready()) {
        dual_frames depth_frame = depth_decoder_->get_current_frame(render_time);
        if (depth_frame.is_valid()) {
            frame.left_depth.hw_buffer  = depth_frame.left_eye.hw_buffer;
            frame.right_depth.hw_buffer = depth_frame.right_eye.hw_buffer;
            frame.has_depth             = true;

            static uint64_t log_counter = 0;
            if (++log_counter % log_interval == 1) {
                spdlog::get("illixr")->debug("[construct_dual_frames] depth: L={} R={}",
                                             static_cast<void*>(frame.left_depth.hw_buffer),
                                             static_cast<void*>(frame.right_depth.hw_buffer));
            }
        }

        // Motion-vector buffers
        if (use_motion_vectors_ && motion_vec_decoder_ && motion_vec_decoder_->is_ready()) {
            dual_frames mv_frame = motion_vec_decoder_->get_current_frame(render_time);
            if (mv_frame.is_valid()) {
                frame.left_motion_vec.hw_buffer  = mv_frame.left_eye.hw_buffer;
                frame.right_motion_vec.hw_buffer = mv_frame.right_eye.hw_buffer;
                frame.has_motion_vectors         = true;

                static uint64_t mv_log_counter = 0;
                if (++mv_log_counter % log_interval == 1) {
                    spdlog::get("illixr")->debug("[construct_dual_frames] Vulkan frame with motion vectors: "
                                                 "MV L={} R={}",
                                                 static_cast<void*>(frame.left_motion_vec.hw_buffer),
                                                 static_cast<void*>(frame.right_motion_vec.hw_buffer));
                }
            }
        }
    }
    return frame;
}

#else

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
    auto current_frame   = frames_reader_.dequeue();
    auto current_latency = network_latency_reader_.get_ro_nullable();
    if (current_frame == nullptr) {
        return false;
    }

    // Store color packet data
    decode_src_color_packets_[0] = current_frame->left_color;
    decode_src_color_packets_[1] = current_frame->right_color;

    // Store depth packet data
    if (use_depth_) {
        spdlog::get("illixr")->info("Use depth");
        decode_src_depth_packets_[0] = current_frame->left_depth;
        decode_src_depth_packets_[1] = current_frame->right_depth;
    }

    // Track frame timing
    if (current_latency != nullptr) {
        log_->info("Network latency (one way): {} ms", current_latency->estimated_one_way_latency_ms);
    } else {
        log_->info("Network latency not available");
    }
    decoded_frame_pose_ = current_frame->pose;
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
    npp_ctx_        = makeNppStreamContext(nullptr);
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
#endif
