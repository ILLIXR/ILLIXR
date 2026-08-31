/**
 * @file offload_rendering_server.cpp
 * @brief Offload Rendering Server Implementation
 *
 * This file implements the server-side component of ILLIXR's offload rendering system.
 * It captures rendered frames, encodes them using hardware-accelerated encoding,
 * and sends them to a remote client for display.
 *
 * Two encoding backends are supported:
 * - FFmpeg with NVENC (default): Uses FFmpeg's Vulkan-CUDA hwaccel pipeline
 * - Direct NVENC (compile with -DNVENC_ENCODER): Uses NVENC directly without FFmpeg
 */
#include "offload_rendering_server.hpp"

#include "illixr/global_module_defs.hpp"

using namespace ILLIXR;
using namespace ILLIXR::data_format;

#ifndef NVENC_ENCODER
using namespace vulkan::ffmpeg_utils;
#elif DUMP_FRAMES
#    include "nvenc/frame_saver_integration.hpp"
#endif

offload_rendering_server::offload_rendering_server(const std::string& name, phonebook* pb)
    : threadloop{name, pb}
    , log_{spdlogger("debug")}
    , switchboard_{pb->lookup_impl<switchboard>()}
    , frames_topic_{switchboard_->get_network_writer<compressed_frame>("compressed_frames",
                                                                       network::topic_config{network::topic_config::BOOST})}
    , pose_relay_{std::make_shared<pose_relay>(name, pb)} {
    // Only encode and pass depth if requested - otherwise skip it.
    use_pass_depth_ = switchboard_->get_env_char("ILLIXR_USE_DEPTH_IMAGES") != nullptr &&
        std::stoi(switchboard_->get_env_char("ILLIXR_USE_DEPTH_IMAGES"));
#ifdef _WIN32
    use_pass_motion_vectors_ = switchboard_->get_env_char("ILLIXR_USE_MOTION_VECTOR_IMAGES") != nullptr &&
        std::stoi(switchboard_->get_env_char("ILLIXR_USE_MOTION_VECTOR_IMAGES"));
#endif
    nalu_only_ = switchboard_->get_env_char("ILLIXR_OFFLOAD_RENDERING_NALU_ONLY") != nullptr &&
        std::stoi(switchboard_->get_env_char("ILLIXR_OFFLOAD_RENDERING_NALU_ONLY"));
#ifdef OPENXR_CLIENT
    const char* overscan_env = std::getenv("ILLIXR_OVERSCAN");
    if (overscan_env != nullptr) {
        overscan_ = std::stof(overscan_env);
    }
#endif
#ifdef _WIN32
    if (use_pass_motion_vectors_) {
        log_->info("Encoding motion vector images for the client");
        // motion vectors require depth data as well
        use_pass_depth_ = true;
    } else {
        log_->info("Not encoding motion vector images for the client");
    }
#endif
    if (use_pass_depth_) {
        log_->debug("Encoding depth images for the client");
    } else {
        log_->debug("Not encoding depth images for the client");
    }

    if (nalu_only_) {
        log_->info("Only sending NALUs to the client");
    }

#ifdef NVENC_ENCODER
    log_->info("Using NVENC encoder (no FFmpeg)");
#else
    log_->info("Using FFmpeg encoder");
#endif
}

void offload_rendering_server::start() {
    pose_relay_->start();
    sender_running_ = true;
    sender_thread_  = std::thread([this]() {
        sender_loop();
    });
    threadloop::start();
}

void offload_rendering_server::stop() {
    pose_relay_->stop();
    sender_running_ = false;
    send_queue_cv_.notify_all();
    if (sender_thread_.joinable())
        sender_thread_.join();
    threadloop::stop();
}

void offload_rendering_server::_p_thread_setup() {
#ifdef OPENXR_CLIENT
    hmd_setup_.recommended_image_width  = (uint32_t) (1680 * overscan_);
    hmd_setup_.recommended_image_height = (uint32_t) (1760 * overscan_);
    for (int eye = 0; eye < 2; eye++) {
        hmd_setup_.fov_angle_left[eye]  = overscan_ * ILLIXR::server_params::fov_left[eye];
        hmd_setup_.fov_angle_right[eye] = overscan_ * ILLIXR::server_params::fov_right[eye];
        hmd_setup_.fov_angle_up[eye]    = overscan_ * ILLIXR::server_params::fov_up[eye];
        hmd_setup_.fov_angle_down[eye]  = overscan_ * ILLIXR::server_params::fov_down[eye];
    }
#endif

    // Wait for display provider to be ready
    while (display_provider_ == nullptr) {
        try {
            display_provider_ = phonebook_->lookup_impl<vulkan::display_provider>();
#ifndef NVENC_ENCODER
            display_provider_ffmpeg = display_provider_;
#endif
        } catch (const std::exception&) {
            log_->debug("Display provider not ready yet");
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    log_->info("Obtained display provider");

    // Configure encoding bitrate from environment or use default
    auto bitrate_env = switchboard_->get_env_char("ILLIXR_OFFLOAD_RENDERING_BITRATE");
    if (bitrate_env == nullptr) {
        bitrate_ = OFFLOAD_RENDERING_BITRATE;
    } else {
        bitrate_ = std::stol(bitrate_env);
    }
    if (bitrate_ <= 0) {
        throw std::runtime_error{"Invalid bitrate value"};
    }
    log_->info("Using bitrate: {}", bitrate_);

    // Configure framerate from environment or use default
    auto framerate_env = switchboard_->get_env_char("ILLIXR_OFFLOAD_RENDERING_FRAMERATE");
    if (framerate_env == nullptr) {
#ifdef OPENXR_CLIENT
        framerate_ = 90;
#else
        framerate_ = 144;
#endif
    } else {
        framerate_ = std::stoi(framerate_env);
    }
    if (framerate_ <= 0) {
        throw std::runtime_error{"Invalid framerate value"};
    }
    log_->info("Using framerate: {}", framerate_);

#ifdef NVENC_ENCODER
    // Initialize Vulkan context for CUDA interop
    nvenc_init_vulkan_context();
#else
    // Initialize FFmpeg and CUDA resources
    ffmpeg_init_device();
    ffmpeg_init_cuda_device();
#endif
    ready_ = true;
}

void offload_rendering_server::setup(VkRenderPass render_pass, uint32_t subpass,
                                     std::shared_ptr<vulkan::buffer_pool<BUFFER_TYPE>> buffer_pool,
                                     bool input_texture_vulkan_coordinates, struct illixr_framebuffer* framebuffer_array,
                                     VkExtent2D extent) {
    (void) render_pass;
    (void) subpass;
    (void) input_texture_vulkan_coordinates;
    // Wait for initialization to complete
    while (!ready_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    buffer_pool_       = buffer_pool;
    framebuffer_array_ = framebuffer_array;
    extent_            = extent;

    log_->info("Setup called:");
    log_->info("  Buffer pool: {}", (void*) buffer_pool.get());
    log_->info("  Framebuffer array: {}", (void*) framebuffer_array);
    log_->info("  Extent: {}x{}", extent.width, extent.height);
#ifdef NVENC_ENCODER
    // Initialize index vectors with -1 (not yet imported)
    color_imported_indices_.assign(OFFLOAD_BUFFER_POOL_SIZE, {-1, -1});
    depth_imported_indices_.assign(OFFLOAD_BUFFER_POOL_SIZE, {-1, -1});
    motion_vec_imported_indices_.assign(OFFLOAD_BUFFER_POOL_SIZE, {-1, -1});

    log_->info("Initialized index vectors for {} buffers", OFFLOAD_BUFFER_POOL_SIZE);

#else
    log_->info("Deferring FFmpeg frame/encoder initialization until first framebuffer is available");
#endif
}

void offload_rendering_server::destroy() {
#ifdef NVENC_ENCODER
    // NVENC encoders clean up in their destructors
    for (auto eye = 0; eye < 2; eye++) {
        color_encoder_[eye].reset();
        depth_encoder_[eye].reset();
    }

    // Clean up Vulkan command pool
    if (vk_ctx_.command_pool && vk_ctx_.device) {
        vkDestroyCommandPool(vk_ctx_.device, vk_ctx_.command_pool, nullptr);
    }
#else
    // Free color frame resources
    for (auto& frame : avvk_color_frames_) {
        for (auto& eye : frame) {
            av_frame_free(&eye.frame);
        }
    }

    // Free depth frame resources if enabled
    if (use_pass_depth_) {
        for (auto& frame : avvk_depth_frames_) {
            for (auto& eye : frame) {
                av_frame_free(&eye.frame);
            }
        }
    }

    // Release FFmpeg contexts
    av_buffer_unref(&frame_ctx_);
    av_buffer_unref(&device_ctx_);
#endif
}

void offload_rendering_server::_p_one_iteration() {
    // -- Iteration cadence measurement -------------------------------------
    // Measures the wall-clock gap between successive _p_one_iteration calls.
    // If this is significantly above 1/90s (~11ms) the threadloop itself is
    // being throttled (e.g. by a sleep or by the switchboard schedule).
    {
        static auto   last_iter_time = std::chrono::high_resolution_clock::now();
        static double iter_gap_acc   = 0.0;
        static int    iter_gap_count = 0;
        const auto    now            = std::chrono::high_resolution_clock::now();
        const double  gap_ms         = std::chrono::duration<double, std::milli>(now - last_iter_time).count();
        last_iter_time               = now;
        iter_gap_acc += gap_ms;
        iter_gap_count++;
        if (iter_gap_count >= 90) {
            iter_gap_acc   = 0.0;
            iter_gap_count = 0;
        }
    }

    // Wait until setup() has been called (framebuffer_array_ and extent_ set)
    if (framebuffer_array_ == nullptr || extent_.width == 0) {
        return;
    }

    // Wait until first frame has been rendered
    if (buffer_pool_ == nullptr || buffer_pool_->latest_decoded_image == -1) {
        return;
    }

    // Import images on first frame ONLY (one-time operation)
    if (!framebuffers_imported_.load()) {
#ifdef NVENC_ENCODER
        log_->info("First frame available - importing framebuffers into NVENC encoders");
        // extent_ is valid (from setup()), so encoders can be initialized
        nvenc_init_encoders();
        nvenc_import_buffer_pool_images();
#else
        log_->info("First frame available - initializing FFmpeg frame contexts and encoders");
        ffmpeg_populate_buffer_pool_from_framebuffers();
        ffmpeg_init_frame_ctx();
        ffmpeg_init_cuda_frame_ctx();
        ffmpeg_init_buffer_pool();
        ffmpeg_init_encoder();

        for (auto eye = 0; eye < 2; eye++) {
            encode_out_color_packets_[eye] = av_packet_alloc();
            if (use_pass_depth_) {
                encode_out_depth_packets_[eye] = av_packet_alloc();
            }
        }
#endif
        framebuffers_imported_.store(true);
    }
    // Record timing for performance analysis
    auto acquire_image_start_time = std::chrono::high_resolution_clock::now();
    frame_timing_[frame_number_]  = acquire_image_start_time;
    // Acquire the latest frame and pose data
    std::pair<ILLIXR::vulkan::image_index_t, BUFFER_TYPE> res =
        buffer_pool_->post_processing_acquire_image(static_cast<signed char>(last_frame_ind_));
    auto acquire_image_end_time = std::chrono::high_resolution_clock::now();

    auto ind   = res.first;
    auto poses = res.second;

    if (ind == -1) {
        return;
    }
    if (ind == last_frame_ind_) {
        // Unity has not submitted a new buffer since last iteration.
        static uint64_t same_buf_drops = 0;
        if (++same_buf_drops % 90 == 1) {
            log_->info("[server_diag] same buffer index drop #{} (ind={}) — "
                       "Unity submit rate may be below iteration rate",
                       same_buf_drops, ind);
        }
        buffer_pool_->post_processing_release_image(ind);
        return;
    }

    last_frame_ind_ = ind;
#ifdef USING_OPENXR
    last_sent_pose_ = poses[0];

    // Find the combined_pose id that corresponds to the eye poses Unity
    // rendered with. Average the two eye orientations to reconstruct the
    // head orientation, then search pose_map_ for the closest match.
    Eigen::Quaternionf render_q{poses[0].orientation.w, poses[0].orientation.x, poses[0].orientation.y, poses[0].orientation.z};

    uint64_t frame_pose_id = pose_relay_->find_pose_id_by_orientation(render_q);
#else
    last_sent_pose_ = poses;
#endif

    // Record encode operation timing
    auto encode_start_time = std::chrono::high_resolution_clock::now();
#ifdef NVENC_ENCODER

    // Encode using NVENC directly
    nvenc_encode_frames(ind);
#else
    // Record copy operation timing
    auto copy_start_time = std::chrono::high_resolution_clock::now();

    // Process color frames for both eyes
    for (auto eye = 0; eye < 2; eye++) {
        // Transfer color frame data to encoding buffer
        auto ret = av_hwframe_transfer_data(encode_src_color_frames_[eye], avvk_color_frames_[ind][eye].frame, 0);
        AV_ASSERT_SUCCESS(ret);
        encode_src_color_frames_[eye]->pts = static_cast<int64_t>(frame_count_++);

        // Process depth frame if enabled
        if (use_pass_depth_) {
            ret = av_hwframe_transfer_data(encode_src_depth_frames_[eye], avvk_depth_frames_[ind][eye].frame, 0);
            AV_ASSERT_SUCCESS(ret);
            encode_src_depth_frames_[eye]->pts = static_cast<int64_t>(frame_count_++);
        }
    }

    auto copy_end_time = std::chrono::high_resolution_clock::now();

    // Encode frames for both eyes
    for (auto eye = 0; eye < 2; eye++) {
        // Encode color frame
        auto ret = avcodec_send_frame(codec_color_ctx_, encode_src_color_frames_[eye]);
        if (ret == AVERROR(EAGAIN)) {
            throw std::runtime_error{"FFmpeg encoder returned EAGAIN. Internal buffer full? Try using a higher-end GPU."};
        }
        AV_ASSERT_SUCCESS(ret);

        // Encode depth frame if enabled
        if (use_pass_depth_) {
            ret = avcodec_send_frame(codec_depth_ctx_, encode_src_depth_frames_[eye]);
            if (ret == AVERROR(EAGAIN)) {
                throw std::runtime_error{"FFmpeg encoder returned EAGAIN. Internal buffer full? Try using a higher-end GPU."};
            }
            AV_ASSERT_SUCCESS(ret);
        }
    }

    // Receive encoded packets
    for (auto eye = 0; eye < 2; eye++) {
        // Receive color packet
        auto ret = avcodec_receive_packet(codec_color_ctx_, encode_out_color_packets_[eye]);
        if (ret == AVERROR(EAGAIN)) {
            throw std::runtime_error{"FFmpeg encoder returned EAGAIN when receiving packets. This should never happen."};
        }
        AV_ASSERT_SUCCESS(ret);

        // Receive depth packet if enabled
        if (use_pass_depth_) {
            ret = avcodec_receive_packet(codec_depth_ctx_, encode_out_depth_packets_[eye]);
            if (ret == AVERROR(EAGAIN)) {
                throw std::runtime_error{"FFmpeg encoder returned EAGAIN when receiving packets. This should never happen."};
            }
            AV_ASSERT_SUCCESS(ret);
        }
    }

    auto copy_time = std::chrono::duration_cast<std::chrono::microseconds>(copy_end_time - copy_start_time).count();
    metrics_["copy_time"] += copy_time;

    // Populate near/far clip planes from the framebuffer array (mirrors what nvenc_encode_frames does).
    if (use_pass_depth_ && framebuffer_array_ != nullptr) {
        near_z_ = framebuffer_array_[ind * 2].near_z;
        far_z_  = framebuffer_array_[ind * 2].far_z;
    } else {
        near_z_ = 0.0f;
        far_z_  = 0.0f;
    }

    // Detect keyframe from the AVPacket flags on eye 0. Both eyes share the same GOP
    // position so eye 0 is sufficient as the reference for the is_keyframe field.
    color_frame_is_keyframe_ =
        (encode_out_color_packets_[0] != nullptr) && (encode_out_color_packets_[0]->flags & AV_PKT_FLAG_KEY) != 0;
#endif
    auto encode_end_time = std::chrono::high_resolution_clock::now();
    buffer_pool_->post_processing_release_image(ind);

    // Calculate timing metrics
    auto encode_time = std::chrono::duration_cast<std::chrono::microseconds>(encode_end_time - encode_start_time).count();
    auto acquire_image_time =
        std::chrono::duration_cast<std::chrono::microseconds>(acquire_image_end_time - acquire_image_start_time).count();

    // Update performance metrics
    metrics_["encode_time"] += encode_time;
    metrics_["acquire_image_time"] += acquire_image_time;

// Send the encoded frame to the client
#ifdef USING_OPENXR
    enqueue_for_network_send(poses, frame_pose_id);
#else
    enqueue_for_network_send(poses);
#endif
    current_encode_time_ = (double) encode_time / 1000.;

    // Update boxcar FPS window and log per-frame encode time + FPS.
    {
        static uint64_t total_encoded = 0;
        total_encoded++;

        const auto now = std::chrono::high_resolution_clock::now();
        fps_window_.push_back(now);
        const auto cutoff = now - std::chrono::seconds(1);
        while (!fps_window_.empty() && fps_window_.front() < cutoff) {
            fps_window_.pop_front();
        }
        const float current_fps = static_cast<float>(fps_window_.size());
    }

    // Log accumulated metrics averages once per second.
    if (!fps_window_.empty()) {
        const double fps              = static_cast<double>(fps_window_.size());
        static auto  last_metrics_log = std::chrono::high_resolution_clock::now();
        const auto   now_check        = std::chrono::high_resolution_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now_check - last_metrics_log).count() >= 1) {
            last_metrics_log = now_check;
            for (auto& metric : metrics_) {
                metric.second = 0;
            }
#ifdef NVENC_ENCODER
            if (use_pass_depth_) {
                log_->info("Depth frame sizes - Left: {} Right: {}", encode_out_depth_packets_[0].size(),
                           encode_out_depth_packets_[1].size());
            }
#else
            if (use_pass_depth_) {
                log_->info("Depth frame sizes - Left: {} Right: {}", encode_out_depth_packets_[0]->size,
                           encode_out_depth_packets_[1]->size);
            }
#endif
        }
    }
    frame_number_++;
}

void offload_rendering_server::enqueue_for_network_send(BUFFER_TYPE& pose
#ifdef USING_OPENXR
                                                        ,
                                                        uint64_t pose_id
#endif
) {
    uint64_t timestamp =
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch())
            .count();

#ifdef NVENC_ENCODER
    // Create compressed frame from NVENC encoded data.
    //
    // COMBINED_ENCODING: the color data is a single combined bitstream in
    // encode_out_combined_color_packet_.  It is passed as the left-eye packet
    // with an empty right-eye packet so the existing compressed_frame wire
    // format is unchanged.  The decoder-side change to interpret this as a
    // combined frame is handled separately.
    //
    // Default: two independent per-eye color bitstreams.
    std::shared_ptr<compressed_frame> frame;
#    ifdef _WIN32
    if (use_pass_motion_vectors_) {
#        ifdef COMBINED_ENCODING
        frame = std::make_shared<compressed_frame>(encode_out_combined_color_packet_, PACKET_TYPE{},
                                                   encode_out_depth_packets_[0], encode_out_depth_packets_[1],
                                                   encode_out_motion_vec_packets_[0], encode_out_motion_vec_packets_[1], pose,
                                                   timestamp, frame_number_, near_z_, far_z_, nalu_only_);
#        else
        frame = std::make_shared<compressed_frame>(encode_out_color_packets_[0], encode_out_color_packets_[1],
                                                   encode_out_depth_packets_[0], encode_out_depth_packets_[1],
                                                   encode_out_motion_vec_packets_[0], encode_out_motion_vec_packets_[1], pose,
                                                   timestamp, frame_number_, near_z_, far_z_, nalu_only_);
#        endif // COMBINED_ENCODING
    } else if (use_pass_depth_) {
#    else
    if (use_pass_depth_) {
#    endif
#    ifdef COMBINED_ENCODING
        frame = std::make_shared<compressed_frame>(encode_out_combined_color_packet_, PACKET_TYPE{},
                                                   encode_out_depth_packets_[0], encode_out_depth_packets_[1], pose, timestamp,
                                                   frame_number_, near_z_, far_z_, nalu_only_);
#    else
        frame = std::make_shared<compressed_frame>(encode_out_color_packets_[0], encode_out_color_packets_[1],
                                                   encode_out_depth_packets_[0], encode_out_depth_packets_[1], pose, timestamp,
                                                   frame_number_, near_z_, far_z_, nalu_only_);
#    endif // COMBINED_ENCODING
    } else {
#    ifdef COMBINED_ENCODING
        frame = std::make_shared<compressed_frame>(encode_out_combined_color_packet_, PACKET_TYPE{}, pose, timestamp,
                                                   frame_number_, nalu_only_);
#    else
        frame = std::make_shared<compressed_frame>(encode_out_color_packets_[0], encode_out_color_packets_[1], pose, timestamp,
                                                   frame_number_, nalu_only_);
#    endif // COMBINED_ENCODING
    }
#    ifdef OPENXR_CLIENT
    // Forward render FOV to headset for correct timewarp with overdraw
    for (int eye = 0; eye < 2; eye++) {
        frame->fov_left[eye]  = hmd_setup_.fov_angle_left[eye];
        frame->fov_right[eye] = hmd_setup_.fov_angle_right[eye];
        frame->fov_up[eye]    = hmd_setup_.fov_angle_up[eye];
        frame->fov_down[eye]  = hmd_setup_.fov_angle_down[eye];
    }
#    endif
#    ifdef USING_OPENXR
    frame->pose_id = pose_id;
#    endif
    frame->is_keyframe = color_frame_is_keyframe_;
    frame->encode_time = current_encode_time_;

    {
        std::lock_guard<std::mutex> lock(send_queue_mutex_);
        while (send_queue_.size() >= MAX_QUEUE_DEPTH) {
            send_queue_.pop_front(); // drops oldest
        }
        send_queue_.push_back(std::move(frame));
    }
    send_queue_cv_.notify_one();
#else
    // FFmpeg path: build the same compressed_frame shape as NVENC and route it
    // through the send queue so the encoding thread is never blocked on network I/O.
    std::shared_ptr<compressed_frame> frame;
#    ifdef _WIN32
    if (use_pass_motion_vectors_) {
        frame = std::make_shared<compressed_frame>(encode_out_color_packets_[0], encode_out_color_packets_[1],
                                                   encode_out_depth_packets_[0], encode_out_depth_packets_[1],
                                                   encode_out_motion_vec_packets_[0], encode_out_motion_vec_packets_[1], pose,
                                                   timestamp, frame_number_, near_z_, far_z_, nalu_only_);
    } else if (use_pass_depth_) {
#    else
    if (use_pass_depth_) {
#    endif
        frame = std::make_shared<compressed_frame>(encode_out_color_packets_[0], encode_out_color_packets_[1],
                                                   encode_out_depth_packets_[0], encode_out_depth_packets_[1], pose, timestamp,
                                                   frame_number_, near_z_, far_z_, nalu_only_);
    } else {
        frame = std::make_shared<compressed_frame>(encode_out_color_packets_[0], encode_out_color_packets_[1], pose, timestamp,
                                                   frame_number_, nalu_only_);
    }

#    ifdef OPENXR_CLIENT
    for (int eye = 0; eye < 2; eye++) {
        frame->fov_left[eye]  = hmd_setup_.fov_angle_left[eye];
        frame->fov_right[eye] = hmd_setup_.fov_angle_right[eye];
        frame->fov_up[eye]    = hmd_setup_.fov_angle_up[eye];
        frame->fov_down[eye]  = hmd_setup_.fov_angle_down[eye];
    }
#    endif
    // frame->pose_id     = pose_id;
#    ifdef USING_OPENXR
    frame->pose_id = pose_id;
#    endif
    frame->is_keyframe = color_frame_is_keyframe_;
    frame->encode_time = current_encode_time_;

    {
        std::lock_guard<std::mutex> lock(send_queue_mutex_);
        while (send_queue_.size() >= MAX_QUEUE_DEPTH) {
            send_queue_.pop_front(); // drops oldest
        }
        send_queue_.push_back(std::move(frame));
    }
    send_queue_cv_.notify_one();
#endif
}

#ifdef NVENC_ENCODER

void offload_rendering_server::nvenc_init_vulkan_context() {
    vk_ctx_.instance              = display_provider_->vk_instance_;
    vk_ctx_.physical_device       = display_provider_->vk_physical_device_;
    vk_ctx_.device                = display_provider_->vk_device_;
    vk_ctx_.graphics_queue        = display_provider_->queues_[vulkan::queue::GRAPHICS].vk_queue;
    vk_ctx_.graphics_queue_family = display_provider_->queues_[vulkan::queue::GRAPHICS].family;

// Get extension function pointers
#    ifdef _WIN32
    vk_ctx_.vkGetMemoryWin32HandleKHR =
        reinterpret_cast<PFN_vkGetMemoryWin32HandleKHR>(vkGetDeviceProcAddr(vk_ctx_.device, "vkGetMemoryWin32HandleKHR"));
    vk_ctx_.vkGetSemaphoreWin32HandleKHR =
        reinterpret_cast<PFN_vkGetSemaphoreWin32HandleKHR>(vkGetDeviceProcAddr(vk_ctx_.device, "vkGetSemaphoreWin32HandleKHR"));

    if (!vk_ctx_.vkGetMemoryWin32HandleKHR) {
        throw std::runtime_error("vkGetMemoryWin32HandleKHR not available - ensure VK_KHR_external_memory_win32 is enabled");
    }
#    else
    vk_ctx_.vkGetMemoryFdKHR = reinterpret_cast<PFN_vkGetMemoryFdKHR>(vkGetDeviceProcAddr(vk_ctx_.device, "vkGetMemoryFdKHR"));
    vk_ctx_.vkGetSemaphoreFdKHR =
        reinterpret_cast<PFN_vkGetSemaphoreFdKHR>(vkGetDeviceProcAddr(vk_ctx_.device, "vkGetSemaphoreFdKHR"));

    if (!vk_ctx_.vkGetMemoryFdKHR) {
        throw std::runtime_error("vkGetMemoryFdKHR not available - ensure VK_KHR_external_memory_fd is enabled");
    }
#    endif

    // Create command pool (may be needed for synchronization)
    VkCommandPoolCreateInfo pool_info = {};
    pool_info.sType                   = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.queueFamilyIndex        = vk_ctx_.graphics_queue_family;
    pool_info.flags                   = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (vkCreateCommandPool(vk_ctx_.device, &pool_info, nullptr, &vk_ctx_.command_pool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create command pool for NVENC");
    }

    log_->info("NVENC: Vulkan context initialized");
}

void offload_rendering_server::nvenc_init_encoders() {
    // extent_ is the native display resolution (per swapchain).
    // The framebuffer images were allocated by Monado at render_scale * native.
    uint32_t encode_width  = extent_.width / 2;
    uint32_t encode_height = extent_.height;

    if (encode_width == 0 || encode_height == 0) {
        throw std::runtime_error("Invalid image dimensions - extent not set");
    }

    // extent_ is the full (both eyes side-by-side) render resolution that Monado produced.
    // Monado's default XRT_COMPOSITOR_SCALE_PERCENTAGE is 140, meaning it renders at 1.4x
    // the native headset resolution.  We want to encode at native resolution, so we divide
    // by the scale factor here.  When running at 100% (SCALE_PERCENTAGE=100) render_scale_
    // is 1.0 and the division is a no-op.
    //
    // The scale factor is read from the same environment variable Monado uses so that the
    // two always stay in sync.

    auto  scale_env    = switchboard_->get_env_int("XRT_COMPOSITOR_SCALE_PERCENTAGE", 140);
    float render_scale = static_cast<float>(scale_env) / 100.0f;

    // Source dimensions are what Monado actually rendered (native * scale).
    // These are informational only — the true values come from fb->image_extent at import time.
    uint32_t source_width  = static_cast<uint32_t>(std::round(encode_width * render_scale));
    uint32_t source_height = static_cast<uint32_t>(std::round(encode_height * render_scale));

    log_->info("NVENC: Monado render scale {:.0f}% -> source {}x{} per eye -> encode {}x{} per eye", render_scale * 100.0f,
               source_width, source_height, encode_width, encode_height);
// Create color encoders.
//
// COMBINED_ENCODING: one encoder at double width encodes both eyes into a
// single bitstream.  color_encoder_[1] is left null.
//
// Default: one encoder per eye at per-eye width.
#    ifdef COMBINED_ENCODING
    {
        const uint32_t combined_width = encode_width * 2;
        log_->info("NVENC: COMBINED_ENCODING — single color encoder {}x{}", combined_width, encode_height);
        color_encoder_[0] = std::make_unique<nvenc_encoder>(combined_width, encode_height, bitrate_, framerate_);
        if (!color_encoder_[0]->initialize(vk_ctx_)) {
            throw std::runtime_error("Failed to initialize combined color encoder");
        }
    }
#    else
    // Encoder dimensions are the target (native) resolution; the source (oversized)
    // Vulkan images are imported separately and the GPU kernel downsamples during
    // color conversion.
    for (int eye = 0; eye < 2; eye++) {
        color_encoder_[eye] = std::make_unique<nvenc_encoder>(encode_width, encode_height, bitrate_, framerate_);

        if (!color_encoder_[eye]->initialize(vk_ctx_)) {
            std::string eye_label = (eye == 0) ? "left" : "right";
            throw std::runtime_error("Failed to initialize " + eye_label + " color encoder");
        }
    }
#    endif // COMBINED_ENCODING
#    ifdef _WIN32
    if (use_pass_motion_vectors_) {
        // Motion vectors are encoded at a fixed 432x432 (the resolution produced by
        // comp_renderer when it blits the Unity quad layer into illixr_framebuffer).
        // A modest bitrate is sufficient because the image carries quantised
        // floating-point velocity values, not visually rich content.
        constexpr uint32_t mv_width   = 432;
        constexpr uint32_t mv_height  = 432;
        constexpr int64_t  mv_bitrate = 10000000; // 10 Mbps

        encode_width  = mv_width;
        encode_height = mv_height;
        log_->info("NVENC: Initializing motion-vector encoders for {}x{} RGBA16F @ {} Mbps", mv_width, mv_height,
                   mv_bitrate / 1000000);

        for (int eye = 0; eye < 2; eye++) {
            motion_vec_encoder_[eye] =
                std::make_unique<nvenc_encoder>(mv_width, mv_height, mv_bitrate, framerate_, encoder_mode::motion_vector);
            if (!motion_vec_encoder_[eye]->initialize(vk_ctx_)) {
                std::string eye_label = (eye == 0) ? "left" : "right";
                throw std::runtime_error("Failed to initialize " + eye_label + " motion-vector encoder");
            }
            log_->info("NVENC: Motion-vector encoder {} initialized", eye);
        }
    }
#    endif
    if (use_pass_depth_) {
        // Depth uses lower bitrate (RG format compresses better than color)
        // Color: 50-100 Mbps, Depth: 15-25 Mbps
        int64_t depth_bitrate = bitrate_ / 4; // 25 Mbps if color is 100 Mbps
        if (depth_bitrate < 15000000) {
            depth_bitrate = 15000000; // Minimum 15 Mbps for quality
        }

        log_->info("NVENC: Initializing depth encoders for {}x{} RG format @ {} Mbps", encode_width, encode_height,
                   depth_bitrate / 1000000);

        for (int eye = 0; eye < 2; eye++) {
            depth_encoder_[eye] =
                std::make_unique<nvenc_encoder>(encode_width, encode_height, depth_bitrate, framerate_, encoder_mode::depth);

            if (!depth_encoder_[eye]->initialize(vk_ctx_)) {
                std::string eye_label = (eye == 0) ? "left" : "right";
                throw std::runtime_error("Failed to initialize " + eye_label + " depth encoder");
            }
            log_->info("NVENC: Depth encoder {} initialized (RG format, {} Mbps)", eye, depth_bitrate / 1000000);
        }
    }

    log_->info("NVENC: All encoders initialized successfully");
}

void offload_rendering_server::nvenc_import_buffer_pool_images() {
    log_->info("Importing Vulkan framebuffers into NVENC encoders");

    // Use framebuffer array passed through setup()
    if (framebuffer_array_ == nullptr) {
        log_->error("Framebuffer array not set - setup() not called?");
        return;
    }

    for (int buffer_idx = 0; buffer_idx < OFFLOAD_BUFFER_POOL_SIZE; buffer_idx++) {
        for (int eye = 0; eye < 2; eye++) {
            int                        fb_idx = buffer_idx * 2 + eye;
            struct illixr_framebuffer* fb     = &framebuffer_array_[fb_idx];

            if (fb->image == VK_NULL_HANDLE) {
                log_->warn("Color image buffer {} eye {} still NULL", buffer_idx, eye);
                continue;
            }
            log_->info("Framebuffer[{}] (buffer={}, eye={}): "
                       "color={}x{} (image={:p}), depth={}x{} (image={:p})",
                       fb_idx, buffer_idx, eye, fb->image_extent.width, fb->image_extent.height, (void*) fb->image,
                       fb->depth_extent.width, fb->depth_extent.height, (void*) fb->depth_image);

            // Import COLOR
            //
            // COMBINED_ENCODING: each eye image is imported into the single combined
            // encoder (color_encoder_[0]).  The per-eye Vulkan image dimensions are
            // passed verbatim; the CUDA texture spans exactly one eye's pixels and the
            // stereo kernel places it in the correct half of the output NV12 buffer.
            //
            // Default: each eye is imported into its own per-eye encoder.
            vulkan_image_info vk_image;
            vk_image.image         = fb->image;
            vk_image.memory        = fb->memory;
            vk_image.memory_size   = fb->image_size;
            vk_image.memory_offset = fb->image_offset;
            vk_image.width         = fb->image_extent.width;
            vk_image.height        = fb->image_extent.height;
            vk_image.format        = VK_FORMAT_R8G8B8A8_UNORM;
            vk_image.tiling        = VK_IMAGE_TILING_OPTIMAL;

#    ifdef COMBINED_ENCODING
            int imported_idx = color_encoder_[0]->import_vulkan_image(vk_image);
#    else
            int imported_idx = color_encoder_[eye]->import_vulkan_image(vk_image);
#    endif // COMBINED_ENCODING
            if (imported_idx >= 0) {
                color_imported_indices_[buffer_idx][eye] = imported_idx;
                log_->info("Imported color buffer {} eye {} -> encoder index {}", buffer_idx, eye, imported_idx);
            }

            // Import DEPTH
            if (use_pass_depth_ && fb->depth_image != VK_NULL_HANDLE) {
                vulkan_image_info depth_vk_image;
                depth_vk_image.image         = fb->depth_image;
                depth_vk_image.memory        = fb->depth_memory;
                depth_vk_image.memory_size   = fb->depth_size;
                depth_vk_image.memory_offset = fb->depth_offset;
                depth_vk_image.width         = fb->depth_extent.width;
                depth_vk_image.height        = fb->depth_extent.height;
                depth_vk_image.format        = VK_FORMAT_R8G8_UNORM;
                depth_vk_image.tiling        = VK_IMAGE_TILING_OPTIMAL;

                int depth_idx = depth_encoder_[eye]->import_vulkan_image(depth_vk_image);
                if (depth_idx >= 0) {
                    depth_imported_indices_[buffer_idx][eye] = depth_idx;
                    log_->info("Imported depth buffer {} eye {} -> encoder index {}", buffer_idx, eye, depth_idx);
                }
            }
#    ifdef _WIN32
            // Import MOTION VECTORS
            if (use_pass_motion_vectors_ && fb->motion_vec_image != VK_NULL_HANDLE) {
                vulkan_image_info mv_vk_image;
                mv_vk_image.image         = fb->motion_vec_image;
                mv_vk_image.memory        = fb->motion_vec_memory;
                mv_vk_image.memory_size   = fb->motion_vec_size;
                mv_vk_image.memory_offset = fb->motion_vec_offset;
                mv_vk_image.width         = fb->motion_vec_extent.width;
                mv_vk_image.height        = fb->motion_vec_extent.height;
                mv_vk_image.format        = VK_FORMAT_R16G16B16A16_SFLOAT;
                mv_vk_image.tiling        = VK_IMAGE_TILING_OPTIMAL;

                int mv_idx = motion_vec_encoder_[eye]->import_vulkan_image(mv_vk_image);
                if (mv_idx >= 0) {
                    motion_vec_imported_indices_[buffer_idx][eye] = mv_idx;
                    log_->info("Imported motion-vector buffer {} eye {} -> encoder index {}", buffer_idx, eye, mv_idx);
                } else {
                    log_->warn("Failed to import motion-vector buffer {} eye {}", buffer_idx, eye);
                }
            } else {
                log_->warn("No MV in buffer");
            }
#    endif
        }
    }
}

void offload_rendering_server::nvenc_encode_frames(int ind) {
    // Bounds check
    if (ind < 0 || ind >= static_cast<int>(color_imported_indices_.size())) {
        log_->error("Buffer index {} out of range (size={})", ind, color_imported_indices_.size());
        return;
    }

    // Capture per-frame near/far clip planes from the depth framebuffer so they can
    // be forwarded to the decoder via compressed_frame.  Eye 0 is used as the
    // reference; both eyes share the same projection clip planes in Unity.
    // Values are 0 when no depth layer was submitted this frame.
    if (use_pass_depth_ && framebuffer_array_ != nullptr) {
        int fb_idx = ind * 2; // eye 0
        near_z_    = framebuffer_array_[fb_idx].near_z;
        far_z_     = framebuffer_array_[fb_idx].far_z;
    } else {
        near_z_ = 0.0f;
        far_z_  = 0.0f;
    }

// Encode color.
//
// COMBINED_ENCODING: call encode_stereo on color_encoder_[0] using both eye
// indices.  The result is a single bitstream containing both eyes side-by-side
// and is stored in encode_out_combined_color_packet_.
//
// Default: encode each eye independently into encode_out_color_packets_[eye].
#    ifdef COMBINED_ENCODING
    {
        const int left_idx  = color_imported_indices_[ind][0];
        const int right_idx = color_imported_indices_[ind][1];
        if (left_idx < 0 || right_idx < 0) {
            log_->warn("Buffer {} color not fully imported yet (left={} right={}), skipping", ind, left_idx, right_idx);
        } else {
            encode_out_combined_color_packet_ = color_encoder_[0]->encode_stereo(left_idx, right_idx);
            color_frame_is_keyframe_          = color_encoder_[0]->last_frame_was_keyframe();
        }
    }
#    else
    for (int eye = 0; eye < 2; eye++) {
        const int color_index = color_imported_indices_[ind][eye];
        if (color_index < 0) {
            log_->warn("Buffer {} eye {} not imported yet, skipping", ind, eye);
            continue;
        }
        encode_out_color_packets_[eye] = color_encoder_[eye]->encode(color_index);
        // Both eyes share the same GOP position; eye 0 is the reference.
        if (eye == 0) {
            color_frame_is_keyframe_ = color_encoder_[0]->last_frame_was_keyframe();
        }
    }
#    endif // COMBINED_ENCODING

    // Depth and motion-vector encoding is unchanged by COMBINED_ENCODING
    // (they remain per-eye because the decoder still expects separate streams).
    for (int eye = 0; eye < 2; eye++) {
        // Encode depth frame if enabled
        if (use_pass_depth_) {
            int depth_index = depth_imported_indices_[ind][eye];
            if (depth_index >= 0) {
                std::vector<uint8_t> encoded_depth = depth_encoder_[eye]->encode(depth_index);
                encode_out_depth_packets_[eye]     = std::move(encoded_depth);
            }
        }

        // Encode motion vector frame if enabled
#    ifdef _WIN32
        if (use_pass_motion_vectors_) {
            int mv_index = motion_vec_imported_indices_[ind][eye];
            if (mv_index >= 0) {
                std::vector<uint8_t> encoded_mv     = motion_vec_encoder_[eye]->encode(mv_index);
                encode_out_motion_vec_packets_[eye] = std::move(encoded_mv);
            } else {
                log_->warn("Motion-vector buffer {} eye {} not imported, skipping", ind, eye);
            }
        }
#    endif
    }
}

#else  // FFmpeg implementation

void offload_rendering_server::ffmpeg_init_device() {
    this->device_ctx_     = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_VULKAN);
    auto hwdev_ctx        = reinterpret_cast<AVHWDeviceContext*>(device_ctx_->data);
    auto vulkan_hwdev_ctx = reinterpret_cast<AVVulkanDeviceContext*>(hwdev_ctx->hwctx);

    // Configure Vulkan device context
    vulkan_hwdev_ctx->inst            = display_provider_->vk_instance_;
    vulkan_hwdev_ctx->phys_dev        = display_provider_->vk_physical_device_;
    vulkan_hwdev_ctx->act_dev         = display_provider_->vk_device_;
    vulkan_hwdev_ctx->device_features = display_provider_->features_;

    // Configure queue families for different operations
    for (auto& queue : display_provider_->queues_) {
        switch (queue.first) {
        case vulkan::queue::GRAPHICS:
            vulkan_hwdev_ctx->queue_family_index    = static_cast<int>(queue.second.family);
            vulkan_hwdev_ctx->nb_graphics_queues    = 1;
            vulkan_hwdev_ctx->queue_family_tx_index = static_cast<int>(queue.second.family);
            vulkan_hwdev_ctx->nb_tx_queues          = 1;
            // TODO: data race here! need to supply the lock_queue and unlock_queue function.
            // Not yet available in release version of ffmpeg
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

    // Vulkan Video not used in current implementation
    vulkan_hwdev_ctx->nb_encode_queues          = 0;
    vulkan_hwdev_ctx->nb_decode_queues          = 0;
    vulkan_hwdev_ctx->queue_family_encode_index = -1;
    vulkan_hwdev_ctx->queue_family_decode_index = -1;

    vulkan_hwdev_ctx->alloc         = nullptr;
    vulkan_hwdev_ctx->get_proc_addr = vkGetInstanceProcAddr;

    // Configure Vulkan extensions
    vulkan_hwdev_ctx->enabled_inst_extensions    = display_provider_->enabled_instance_extensions_.data();
    vulkan_hwdev_ctx->nb_enabled_inst_extensions = static_cast<int>(display_provider_->enabled_instance_extensions_.size());
    vulkan_hwdev_ctx->enabled_dev_extensions     = display_provider_->enabled_device_extensions_.data();
    vulkan_hwdev_ctx->nb_enabled_dev_extensions  = static_cast<int>(display_provider_->enabled_device_extensions_.size());

    vulkan_hwdev_ctx->lock_queue   = &ffmpeg_lock_queue;
    vulkan_hwdev_ctx->unlock_queue = &ffmpeg_unlock_queue;

    AV_ASSERT_SUCCESS(av_hwdevice_ctx_init(device_ctx_));
    log_->info("FFmpeg Vulkan hwdevice context initialized");
}

void offload_rendering_server::ffmpeg_init_cuda_device() {
    auto ret = av_hwdevice_ctx_create(&cuda_device_ctx_, AV_HWDEVICE_TYPE_CUDA, nullptr, nullptr, 0);
    AV_ASSERT_SUCCESS(ret);
    if (cuda_device_ctx_ == nullptr) {
        throw std::runtime_error{"Failed to create FFmpeg CUDA hwdevice context"};
    }
    log_->info("FFmpeg CUDA hwdevice context initialized");
}

void offload_rendering_server::ffmpeg_init_frame_ctx() {
    assert(this->buffer_pool_ != nullptr);
    this->frame_ctx_ = av_hwframe_ctx_alloc(device_ctx_);
    if (!frame_ctx_) {
        throw std::runtime_error{"Failed to create FFmpeg Vulkan hwframe context"};
    }

    auto hwframe_ctx    = reinterpret_cast<AVHWFramesContext*>(frame_ctx_->data);
    hwframe_ctx->format = AV_PIX_FMT_VULKAN;

    // Configure pixel format based on Vulkan image format
    auto pix_format = vulkan::ffmpeg_utils::get_pix_format_from_vk_format(buffer_pool_->image_pool[0][0].image_info.format);
    if (!pix_format) {
        throw std::runtime_error{"Unsupported Vulkan image format " +
                                 std::to_string(buffer_pool_->image_pool[0][0].image_info.format) +
                                 " when creating FFmpeg Vulkan hwframe context"};
    }

    // Set frame properties
    hwframe_ctx->sw_format         = *pix_format;
    hwframe_ctx->width             = static_cast<int>(buffer_pool_->image_pool[0][0].image_info.extent.width);
    hwframe_ctx->height            = static_cast<int>(buffer_pool_->image_pool[0][0].image_info.extent.height);
    hwframe_ctx->initial_pool_size = 0;

    auto ret = av_hwframe_ctx_init(frame_ctx_);
    AV_ASSERT_SUCCESS(ret);
}

void offload_rendering_server::ffmpeg_init_cuda_frame_ctx() {
    assert(this->buffer_pool_ != nullptr);
    auto cuda_frame_ref = av_hwframe_ctx_alloc(cuda_device_ctx_);
    if (!cuda_frame_ref) {
        throw std::runtime_error{"Failed to create FFmpeg CUDA hwframe context"};
    }

    // Configure CUDA frame properties
    auto cuda_hwframe_ctx       = reinterpret_cast<AVHWFramesContext*>(cuda_frame_ref->data);
    cuda_hwframe_ctx->format    = AV_PIX_FMT_CUDA;
    cuda_hwframe_ctx->sw_format = AV_PIX_FMT_BGRA;
    cuda_hwframe_ctx->width     = static_cast<int>(buffer_pool_->image_pool[0][0].image_info.extent.width);
    cuda_hwframe_ctx->height    = static_cast<int>(buffer_pool_->image_pool[0][0].image_info.extent.height);

    auto ret = av_hwframe_ctx_init(cuda_frame_ref);
    AV_ASSERT_SUCCESS(ret);
    this->cuda_frame_ctx_ = cuda_frame_ref;
}

void offload_rendering_server::ffmpeg_populate_buffer_pool_from_framebuffers() {
    assert(this->buffer_pool_ != nullptr);
    if (framebuffer_array_ == nullptr) {
        throw std::runtime_error{"Cannot initialize FFmpeg frames before Monado framebuffer array is available"};
    }

    for (size_t buffer_idx = 0; buffer_idx < buffer_pool_->image_pool.size(); buffer_idx++) {
        for (size_t eye = 0; eye < 2; eye++) {
            const size_t                     fb_idx = buffer_idx * 2 + eye;
            const struct illixr_framebuffer& fb     = framebuffer_array_[fb_idx];

            if (fb.image == VK_NULL_HANDLE || fb.memory == VK_NULL_HANDLE || fb.image_extent.width == 0 ||
                fb.image_extent.height == 0) {
                throw std::runtime_error{"Monado framebuffer " + std::to_string(fb_idx) +
                                         " is not ready for FFmpeg initialization"};
            }

            auto& image                        = buffer_pool_->image_pool[buffer_idx][eye];
            image.image                        = fb.image;
            image.image_view                   = fb.view;
            image.allocation_info.deviceMemory = fb.memory;
            image.allocation_info.size         = fb.image_size;
            image.allocation_info.offset       = fb.image_offset;
            image.image_info                   = {
                VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                nullptr,
                0,
                VK_IMAGE_TYPE_2D,
                VK_FORMAT_R8G8B8A8_UNORM,
                {fb.image_extent.width, fb.image_extent.height, 1},
                1,
                1,
                VK_SAMPLE_COUNT_1_BIT,
                VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_SHARING_MODE_EXCLUSIVE,
                0,
                nullptr,
                VK_IMAGE_LAYOUT_UNDEFINED,
            };

            if (use_pass_depth_ && fb.depth_image != VK_NULL_HANDLE) {
                auto& depth_image                        = buffer_pool_->depth_image_pool[buffer_idx][eye];
                depth_image.image                        = fb.depth_image;
                depth_image.image_view                   = fb.depth_view;
                depth_image.allocation_info.deviceMemory = fb.depth_memory;
                depth_image.allocation_info.size         = fb.depth_size;
                depth_image.allocation_info.offset       = fb.depth_offset;
                depth_image.image_info                   = {
                    VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                    nullptr,
                    0,
                    VK_IMAGE_TYPE_2D,
                    VK_FORMAT_R8G8_UNORM,
                    {fb.depth_extent.width, fb.depth_extent.height, 1},
                    1,
                    1,
                    VK_SAMPLE_COUNT_1_BIT,
                    VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_SHARING_MODE_EXCLUSIVE,
                    0,
                    nullptr,
                    VK_IMAGE_LAYOUT_UNDEFINED,
                };
            }
        }
    }
}

void offload_rendering_server::ffmpeg_init_buffer_pool() {
    assert(this->buffer_pool_ != nullptr);

    // Initialize color frame arrays
    avvk_color_frames_.resize(buffer_pool_->image_pool.size());
    if (use_pass_depth_) {
        avvk_depth_frames_.resize(buffer_pool_->depth_image_pool.size());
    }

    // Set up frames for each buffer in the pool
    for (size_t i = 0; i < buffer_pool_->image_pool.size(); i++) {
        for (size_t eye = 0; eye < 2; eye++) {
            // Create and configure color frame
            auto vk_frame = av_vk_frame_alloc();
            if (!vk_frame) {
                throw std::runtime_error{"Failed to allocate FFmpeg Vulkan frame for color image"};
            }

            // Configure Vulkan frame properties
            vk_frame->img[0]          = buffer_pool_->image_pool[i][eye].image;
            vk_frame->tiling          = buffer_pool_->image_pool[i][eye].image_info.tiling;
            vk_frame->mem[0]          = buffer_pool_->image_pool[i][eye].allocation_info.deviceMemory;
            vk_frame->size[0]         = buffer_pool_->image_pool[i][eye].allocation_info.size;
            vk_frame->offset[0]       = static_cast<ptrdiff_t>(buffer_pool_->image_pool[i][eye].allocation_info.offset);
            vk_frame->queue_family[0] = display_provider_->queues_[vulkan::queue::GRAPHICS].family;

            // Create and configure semaphore for synchronization
            VkExportSemaphoreCreateInfo export_semaphore_create_info{VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO, nullptr,
                                                                     VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT};
            vk_frame->sem[0] =
                vulkan::create_timeline_semaphore(display_provider_->vk_device_, 0, &export_semaphore_create_info);
            vk_frame->sem_value[0] = 0;
            vk_frame->layout[0]    = VK_IMAGE_LAYOUT_UNDEFINED;

            avvk_color_frames_[i][eye].vk_frame = vk_frame;

            // Create and configure AVFrame for color
            auto av_frame = av_frame_alloc();
            if (!av_frame) {
                throw std::runtime_error{"Failed to allocate FFmpeg frame for color image"};
            }
            av_frame->format                 = AV_PIX_FMT_VULKAN;
            av_frame->width                  = static_cast<int>(buffer_pool_->image_pool[i][eye].image_info.extent.width);
            av_frame->height                 = static_cast<int>(buffer_pool_->image_pool[i][eye].image_info.extent.height);
            av_frame->hw_frames_ctx          = av_buffer_ref(frame_ctx_);
            av_frame->data[0]                = reinterpret_cast<uint8_t*>(vk_frame);
            av_frame->buf[0]                 = av_buffer_create(av_frame->data[0], 0, [](void*, uint8_t*) { }, nullptr, 0);
            av_frame->pts                    = 0;
            avvk_color_frames_[i][eye].frame = av_frame;

            // Set up depth frame if enabled
            if (use_pass_depth_) {
                auto vk_depth_frame = av_vk_frame_alloc();
                if (!vk_depth_frame) {
                    throw std::runtime_error{"Failed to allocate FFmpeg Vulkan frame for depth image"};
                }

                // Configure Vulkan depth frame properties
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
                vk_depth_frame->layout[0]    = VK_IMAGE_LAYOUT_UNDEFINED;

                avvk_depth_frames_[i][eye].vk_frame = vk_depth_frame;

                // Create and configure AVFrame for depth
                auto av_depth_frame = av_frame_alloc();
                if (!av_depth_frame) {
                    throw std::runtime_error{"Failed to allocate FFmpeg frame for depth image"};
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

    // Initialize source frames for encoding
    for (size_t eye = 0; eye < 2; eye++) {
        // Set up color source frame
        encode_src_color_frames_[eye]                = av_frame_alloc();
        encode_src_color_frames_[eye]->format        = AV_PIX_FMT_CUDA;
        encode_src_color_frames_[eye]->hw_frames_ctx = av_buffer_ref(cuda_frame_ctx_);
        encode_src_color_frames_[eye]->width         = static_cast<int>(buffer_pool_->image_pool[0][0].image_info.extent.width);
        encode_src_color_frames_[eye]->height = static_cast<int>(buffer_pool_->image_pool[0][0].image_info.extent.height);

        // Configure color space properties
        encode_src_color_frames_[eye]->color_range     = AVCOL_RANGE_JPEG;
        encode_src_color_frames_[eye]->colorspace      = AVCOL_SPC_BT709;
        encode_src_color_frames_[eye]->color_trc       = AVCOL_TRC_BT709;
        encode_src_color_frames_[eye]->color_primaries = AVCOL_PRI_BT709;
        encode_src_color_frames_[eye]->pict_type       = AV_PICTURE_TYPE_I;

        auto ret = av_hwframe_get_buffer(cuda_frame_ctx_, encode_src_color_frames_[eye], 0);
        AV_ASSERT_SUCCESS(ret);

        // Set up depth source frame if enabled
        if (use_pass_depth_) {
            encode_src_depth_frames_[eye]                = av_frame_alloc();
            encode_src_depth_frames_[eye]->format        = AV_PIX_FMT_CUDA;
            encode_src_depth_frames_[eye]->hw_frames_ctx = av_buffer_ref(cuda_frame_ctx_);
            encode_src_depth_frames_[eye]->width =
                static_cast<int>(buffer_pool_->depth_image_pool[0][0].image_info.extent.width);
            encode_src_depth_frames_[eye]->height =
                static_cast<int>(buffer_pool_->depth_image_pool[0][0].image_info.extent.height);

            // Configure depth frame color space
            encode_src_depth_frames_[eye]->color_range     = AVCOL_RANGE_JPEG;
            encode_src_depth_frames_[eye]->colorspace      = AVCOL_SPC_BT709;
            encode_src_depth_frames_[eye]->color_trc       = AVCOL_TRC_BT709;
            encode_src_depth_frames_[eye]->color_primaries = AVCOL_PRI_BT709;
            encode_src_depth_frames_[eye]->pict_type       = AV_PICTURE_TYPE_I;

            ret = av_hwframe_get_buffer(cuda_frame_ctx_, encode_src_depth_frames_[eye], 0);
            AV_ASSERT_SUCCESS(ret);
        }
    }
}

void offload_rendering_server::ffmpeg_init_encoder() {
    // Find hardware-accelerated encoder
    auto encoder = avcodec_find_encoder_by_name(OFFLOAD_RENDERING_FFMPEG_ENCODER_NAME);
    if (!encoder) {
        throw std::runtime_error{"Failed to find FFmpeg encoder"};
    }

    // Initialize color encoder
    this->codec_color_ctx_ = avcodec_alloc_context3(encoder);
    if (!codec_color_ctx_) {
        throw std::runtime_error{"Failed to allocate FFmpeg encoder context for color images"};
    }

    // Configure multithreading
    codec_color_ctx_->thread_count = 0; // auto
    codec_color_ctx_->thread_type  = FF_THREAD_SLICE;

    // Configure pixel format and hardware acceleration
    codec_color_ctx_->pix_fmt       = AV_PIX_FMT_CUDA;
    codec_color_ctx_->sw_pix_fmt    = AV_PIX_FMT_BGRA;
    codec_color_ctx_->hw_frames_ctx = av_buffer_ref(cuda_frame_ctx_);

    // Set frame dimensions and timing
    codec_color_ctx_->width     = static_cast<int>(buffer_pool_->image_pool[0][0].image_info.extent.width);
    codec_color_ctx_->height    = static_cast<int>(buffer_pool_->image_pool[0][0].image_info.extent.height);
    codec_color_ctx_->time_base = {1, framerate_};
    codec_color_ctx_->framerate = {framerate_, 1};
    codec_color_ctx_->bit_rate  = bitrate_;

    // Configure color space
    codec_color_ctx_->color_range     = AVCOL_RANGE_JPEG;
    codec_color_ctx_->colorspace      = AVCOL_SPC_BT709;
    codec_color_ctx_->color_trc       = AVCOL_TRC_BT709;
    codec_color_ctx_->color_primaries = AVCOL_PRI_BT709;

    // Configure for low latency
    codec_color_ctx_->max_b_frames = 0;
    codec_color_ctx_->gop_size     = 15; // Intra-frame interval
    av_opt_set_int(codec_color_ctx_->priv_data, "zerolatency", 1, 0);
    av_opt_set_int(codec_color_ctx_->priv_data, "delay", 0, 0);

    auto ret = avcodec_open2(codec_color_ctx_, encoder, nullptr);
    AV_ASSERT_SUCCESS(ret);

    // Initialize depth encoder if enabled
    if (use_pass_depth_) {
        this->codec_depth_ctx_ = avcodec_alloc_context3(encoder);
        if (!codec_depth_ctx_) {
            throw std::runtime_error{"Failed to allocate FFmpeg encoder context for depth images"};
        }

        // Configure multithreading
        codec_depth_ctx_->thread_count = 0;
        codec_depth_ctx_->thread_type  = FF_THREAD_SLICE;

        // Configure pixel format and hardware acceleration
        codec_depth_ctx_->pix_fmt       = AV_PIX_FMT_CUDA;
        codec_depth_ctx_->sw_pix_fmt    = AV_PIX_FMT_BGRA;
        codec_depth_ctx_->hw_frames_ctx = av_buffer_ref(cuda_frame_ctx_);

        // Set frame dimensions and timing
        codec_depth_ctx_->width     = static_cast<int>(buffer_pool_->depth_image_pool[0][0].image_info.extent.width);
        codec_depth_ctx_->height    = static_cast<int>(buffer_pool_->depth_image_pool[0][0].image_info.extent.height);
        codec_depth_ctx_->time_base = {1, framerate_};
        codec_depth_ctx_->framerate = {framerate_, 1};
        codec_depth_ctx_->bit_rate  = bitrate_;

        // Configure color space
        codec_depth_ctx_->color_range     = AVCOL_RANGE_JPEG;
        codec_depth_ctx_->colorspace      = AVCOL_SPC_BT709;
        codec_depth_ctx_->color_trc       = AVCOL_TRC_BT709;
        codec_depth_ctx_->color_primaries = AVCOL_PRI_BT709;

        // Configure for low latency
        codec_depth_ctx_->max_b_frames = 0;
        codec_depth_ctx_->gop_size     = 15;
        av_opt_set_int(codec_depth_ctx_->priv_data, "zerolatency", 1, 0);
        av_opt_set_int(codec_depth_ctx_->priv_data, "delay", 0, 0);

        ret = avcodec_open2(codec_depth_ctx_, encoder, nullptr);
        AV_ASSERT_SUCCESS(ret);
    }
}
#endif // NVENC_ENCODER

void offload_rendering_server::sender_loop() {
    while (sender_running_) {
        std::shared_ptr<data_format::compressed_frame> frame;
        {
            std::unique_lock<std::mutex> lock(send_queue_mutex_);
            send_queue_cv_.wait(lock, [this] {
                return !send_queue_.empty() || !sender_running_;
            });
            if (!sender_running_ && send_queue_.empty())
                break;
            frame = std::move(send_queue_.front());
            send_queue_.pop_front();
        }
        if (pose_usage_.count(frame->pose_id) == 0) {
            pose_usage_[frame->pose_id] = 1;
        } else {
            pose_usage_[frame->pose_id]++;
        }

        frames_topic_.put(std::move(frame));
    }
}
