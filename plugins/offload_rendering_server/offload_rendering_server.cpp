#include "offload_rendering_server.hpp"

using namespace ILLIXR;
using namespace ILLIXR::data_format;
using namespace vulkan::ffmpeg_utils;

offload_rendering_server::offload_rendering_server(const std::string& name, phonebook* pb)
    : threadloop{name, pb}
    , log_{spdlogger("debug")}
    , switchboard_{pb->lookup_impl<switchboard>()}
    , frames_topic_{switchboard_->get_network_writer<compressed_frame>("compressed_frames", {})}
    , render_pose_{switchboard_->get_reader<fast_pose_type>("render_pose")} {
    // Only encode and pass depth if requested - otherwise skip it.
    use_pass_depth_ = switchboard_->get_env_char("ILLIXR_USE_DEPTH_IMAGES") != nullptr &&
        std::stoi(switchboard_->get_env_char("ILLIXR_USE_DEPTH_IMAGES"));
    nalu_only_ = switchboard_->get_env_char("ILLIXR_OFFLOAD_RENDERING_NALU_ONLY") != nullptr &&
        std::stoi(switchboard_->get_env_char("ILLIXR_OFFLOAD_RENDERING_NALU_ONLY"));
    if (use_pass_depth_) {
        log_->debug("Encoding depth images for the client");
    } else {
        log_->debug("Not encoding depth images for the client");
    }

    if (nalu_only_) {
        log_->info("Only sending NALUs to the client");
    }
    spd_add_file_sink("fps", "csv", "warn");
    log_->warn("Log Time,FPS");
}

void offload_rendering_server::start() {
    threadloop::start();
}

void offload_rendering_server::_p_thread_setup() {
    // Wait for display provider to be ready
    while (display_provider_ == nullptr) {
        try {
            display_provider_       = phonebook_->lookup_impl<vulkan::display_provider>();
            display_provider_ffmpeg = display_provider_;
        } catch (const std::exception& e) {
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
        framerate_ = 144;
    } else {
        framerate_ = std::stoi(framerate_env);
    }
    if (framerate_ <= 0) {
        throw std::runtime_error{"Invalid framerate value"};
    }
    log_->info("Using framerate: {}", framerate_);

    // Initialize FFmpeg and CUDA resources
    ffmpeg_init_device();
    ffmpeg_init_cuda_device();
    ready_ = true;
}

void offload_rendering_server::setup(VkRenderPass render_pass, uint32_t subpass,
                                     std::shared_ptr<vulkan::buffer_pool<fast_pose_type>> buffer_pool,
                                     bool                                                 input_texture_vulkan_coordinates) {
    (void) render_pass;
    (void) subpass;
    (void) input_texture_vulkan_coordinates;
    // Wait for initialization to complete
    while (!ready_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    this->buffer_pool_ = buffer_pool;

    // Initialize FFmpeg frame contexts and encoders
    ffmpeg_init_frame_ctx();
    ffmpeg_init_cuda_frame_ctx();
    ffmpeg_init_buffer_pool();
    ffmpeg_init_encoder();

    // Allocate output packets for encoded frames
    for (auto eye = 0; eye < 2; eye++) {
        encode_out_color_packets_[eye] = av_packet_alloc();
        if (use_pass_depth_) {
            encode_out_depth_packets_[eye] = av_packet_alloc();
        }
    }
}

void offload_rendering_server::destroy() {
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
}

fast_pose_type offload_rendering_server::get_fast_pose() const {
    auto pose = render_pose_.get_ro_nullable();
    if (pose == nullptr) {
        return {};
    } else {
        return *pose;
    }
}

void offload_rendering_server::_p_one_iteration() {
    // Skip if no new frame is available
    if (buffer_pool_ == nullptr || buffer_pool_->latest_decoded_image == -1) {
        log_->info("no decoded image, returning");
        return;
    }

    // Record timing for performance analysis
    auto acquire_image_start_time = std::chrono::high_resolution_clock::now();

    // Acquire the latest frame and pose data
    std::pair<ILLIXR::vulkan::image_index_t, fast_pose_type> res =
        buffer_pool_->post_processing_acquire_image(static_cast<signed char>(last_frame_ind_));
    auto acquire_image_end_time = std::chrono::high_resolution_clock::now();

    auto ind  = res.first;
    auto pose = res.second;

    if (ind == -1) {
        return;
    }
    last_frame_ind_ = static_cast<unsigned char>(ind);

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
    buffer_pool_->post_processing_release_image(ind);

    // Record encode operation timing
    auto encode_start_time = std::chrono::high_resolution_clock::now();

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
    auto encode_end_time = std::chrono::high_resolution_clock::now();

    // Calculate timing metrics
    auto copy_time   = std::chrono::duration_cast<std::chrono::microseconds>(copy_end_time - copy_start_time).count();
    auto encode_time = std::chrono::duration_cast<std::chrono::microseconds>(encode_end_time - encode_start_time).count();
    auto acquire_image_time =
        std::chrono::duration_cast<std::chrono::microseconds>(acquire_image_end_time - acquire_image_start_time).count();

    // Update performance metrics
    metrics_["copy_time"] += copy_time;
    metrics_["encode_time"] += encode_time;
    metrics_["acquire_image_time"] += acquire_image_time;

    // Send encoded frame to client
    enqueue_for_network_send(pose);

    // Log performance metrics every second
    if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - fps_start_time_).count() >=
        1) {
        log_->info("Encoder FPS: {}", fps_counter_);
        fps_start_time_ = std::chrono::high_resolution_clock::now();
        log_->warn("{},{}", fps_start_time_.time_since_epoch().count(), fps_counter_);

        for (auto& metric : metrics_) {
            double fps = std::max(fps_counter_, (double) 0);
            log_->info("{}: {}", metric.first, metric.second / fps);
            metric.second = 0;
        }

        if (use_pass_depth_) {
            log_->info("Depth frame sizes - Left: {} Right: {}", encode_out_depth_packets_[0]->size,
                       encode_out_depth_packets_[1]->size);
            // std::cout << "depth left: " << encode_out_depth_packets_[0]->size << " depth right: " <<
            // encode_out_depth_packets_[0]->size << std::endl;
        }

        fps_counter_ = 0;
    } else {
        fps_counter_++;
    }
}

void offload_rendering_server::enqueue_for_network_send(fast_pose_type& pose) {
    uint64_t timestamp =
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch())
            .count();

    if (use_pass_depth_) {
        frames_topic_.put(std::make_shared<compressed_frame>(encode_out_color_packets_[0], encode_out_color_packets_[1],
                                                             encode_out_depth_packets_[0], encode_out_depth_packets_[1], pose,
                                                             timestamp, nalu_only_));
    } else {
        frames_topic_.put(std::make_shared<compressed_frame>(encode_out_color_packets_[0], encode_out_color_packets_[1], pose,
                                                             timestamp, nalu_only_));
    }
}

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
        throw std::runtime_error{"Unsupported Vulkan image format when creating FFmpeg Vulkan hwframe context"};
    }
    assert(pix_format == AV_PIX_FMT_BGRA);

    // Set frame properties
    hwframe_ctx->sw_format         = AV_PIX_FMT_BGRA;
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
