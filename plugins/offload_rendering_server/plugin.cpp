/**
 * @file plugin.cpp
 * @brief Offload Rendering Server Plugin Implementation
 *
 * This plugin implements the server-side component of ILLIXR's offload rendering system.
 * It captures rendered frames, encodes them using hardware-accelerated H.264/HEVC encoding,
 * and sends them to a remote client for display. The system supports both color and depth
 * frame transmission, with configurable encoding parameters.
 */

#include "illixr/data_format.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/pose_prediction.hpp"
#include "illixr/serializable_data.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"
#include "illixr/vk/display_provider.hpp"
#include "illixr/vk/ffmpeg_utils.hpp"
#include "illixr/vk/render_pass.hpp"
#include "illixr/vk/vk_extension_request.h"
#include "illixr/vk/vulkan_utils.hpp"

#include <set>

using namespace ILLIXR;
using namespace ILLIXR::vulkan::ffmpeg_utils;

/**
 * @class offload_rendering_server
 * @brief Main server implementation for offload rendering
 *
 * This class handles:
 * 1. Frame capture from the rendering pipeline
 * 2. Hardware-accelerated encoding using FFmpeg/CUDA
 * 3. Network transmission of encoded frames
 * 4. Pose synchronization with the client
 */
class offload_rendering_server
    : public threadloop
    , public vulkan::timewarp
    , public pose_prediction
    , std::enable_shared_from_this<plugin> {
public:
    /**
     * @brief Constructor initializes the server with configuration from environment variables
     * @param name Plugin name
     * @param pb Phonebook for component lookup
     */
    offload_rendering_server(const std::string& name, phonebook* pb)
        : threadloop{name, pb}
        , log{spdlogger("debug")}
        , sb{pb->lookup_impl<switchboard>()}
        , frames_topic{std::move(sb->get_network_writer<compressed_frame>("compressed_frames", {}))}
        , render_pose{sb->get_reader<fast_pose_type>("render_pose")} {
        // Only encode and pass depth if requested - otherwise skip it.
        pass_depth = std::getenv("ILLIXR_USE_DEPTH_IMAGES") != nullptr && std::stoi(std::getenv("ILLIXR_USE_DEPTH_IMAGES"));
        nalu_only  = std::getenv("ILLIXR_OFFLOAD_RENDERING_NALU_ONLY") != nullptr &&
            std::stoi(std::getenv("ILLIXR_OFFLOAD_RENDERING_NALU_ONLY"));
        if (pass_depth) {
            log->debug("Encoding depth images for the client");
        } else {
            log->debug("Not encoding depth images for the client");
        }

        if (nalu_only) {
            log->info("Only sending NALUs to the client");
        }
        spd_add_file_sink("fps", "csv", "warn");
        log->warn("Log Time,FPS");
    }

    void start() override {
        threadloop::start();
    }

    void _p_thread_setup() override {
        // Wait for display provider to be ready
        while (dp == nullptr) {
            try {
                dp                      = pb->lookup_impl<vulkan::display_provider>();
                display_provider_ffmpeg = dp;
            } catch (const std::exception& e) {
                log->debug("Display provider not ready yet");
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
        log->info("Obtained display provider");

        // Configure encoding bitrate from environment or use default
        auto bitrate_env = std::getenv("ILLIXR_OFFLOAD_RENDERING_BITRATE");
        if (bitrate_env == nullptr) {
            bitrate = OFFLOAD_RENDERING_BITRATE;
        } else {
            bitrate = std::stol(bitrate_env);
        }
        if (bitrate <= 0) {
            throw std::runtime_error{"Invalid bitrate value"};
        }
        log->info("Using bitrate: {}", bitrate);

        // Configure framerate from environment or use default
        auto framerate_env = std::getenv("ILLIXR_OFFLOAD_RENDERING_FRAMERATE");
        if (framerate_env == nullptr) {
            framerate = 144;
        } else {
            framerate = std::stoi(framerate_env);
        }
        if (framerate <= 0) {
            throw std::runtime_error{"Invalid framerate value"};
        }
        log->info("Using framerate: {}", framerate);

        // Initialize FFmpeg and CUDA resources
        ffmpeg_init_device();
        ffmpeg_init_cuda_device();
        ready = true;
    }

    /**
     * @brief Sets up the rendering pipeline and encoding resources
     * @param render_pass Vulkan render pass handle
     * @param subpass Subpass index
     * @param _buffer_pool Buffer pool for frame management
     * @param input_texture_vulkan_coordinates Whether input textures use Vulkan coordinates
     */
    void setup(VkRenderPass render_pass, uint32_t subpass, std::shared_ptr<vulkan::buffer_pool<fast_pose_type>> _buffer_pool,
               bool input_texture_vulkan_coordinates) override {
        // Wait for initialization to complete
        while (!ready) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        this->buffer_pool = _buffer_pool;

        // Initialize FFmpeg frame contexts and encoders
        ffmpeg_init_frame_ctx();
        ffmpeg_init_cuda_frame_ctx();
        ffmpeg_init_buffer_pool();
        ffmpeg_init_encoder();

        // Allocate output packets for encoded frames
        for (auto eye = 0; eye < 2; eye++) {
            encode_out_color_packets[eye] = av_packet_alloc();
            if (pass_depth) {
                encode_out_depth_packets[eye] = av_packet_alloc();
            }
        }
    }

    /**
     * @brief Indicates this sink does not make use of the rendering pipeline in order for the access masks of the layout
     * transitions to be set properly
     */
    bool is_external() override {
        return true;
    }

    /**
     * @brief Cleanup resources on destruction
     */
    void destroy() override {
        // Free color frame resources
        for (auto& frame : avvk_color_frames) {
            for (auto& eye : frame) {
                av_frame_free(&eye.frame);
            }
        }

        // Free depth frame resources if enabled
        if (pass_depth) {
            for (auto& frame : avvk_depth_frames) {
                for (auto& eye : frame) {
                    av_frame_free(&eye.frame);
                }
            }
        }

        // Release FFmpeg contexts
        av_buffer_unref(&frame_ctx);
        av_buffer_unref(&device_ctx);
    }

    /**
     * @brief Get the latest pose for rendering
     */
    fast_pose_type get_fast_pose() const override {
        auto pose = render_pose.get_ro_nullable();
        if (pose == nullptr) {
            return {};
        } else {
            return *pose;
        }
    }

    /**
     * @brief Get the true pose (same as fast pose in this implementation)
     */
    pose_type get_true_pose() const override {
        return get_fast_pose().pose;
    }

    /**
     * @brief Get predicted pose for a future time point (returns current pose)
     */
    fast_pose_type get_fast_pose(time_point future_time) const override {
        return get_fast_pose();
    }

    /**
     * @brief Check if fast pose data is reliable
     */
    bool fast_pose_reliable() const override {
        return render_pose.get_ro_nullable() != nullptr;
    }

    /**
     * @brief Check if true pose data is reliable (always false in this implementation)
     */
    bool true_pose_reliable() const override {
        return false;
    }

    /**
     * @brief Set orientation offset (no-op in this implementation)
     */
    void set_offset(const Eigen::Quaternionf& orientation) override { }

    /**
     * @brief Get orientation offset (returns identity in this implementation)
     */
    Eigen::Quaternionf get_offset() override {
        return Eigen::Quaternionf();
    }

    /**
     * @brief Correct pose data (no-op in this implementation)
     */
    pose_type correct_pose(const pose_type& pose) const override {
        return pose_type();
    }

    /**
     * @brief Record command buffer (no-op in this implementation)
     */
    void record_command_buffer(VkCommandBuffer commandBuffer, VkFramebuffer framebuffer, int buffer_ind, bool left) override { }

    /**
     * @brief Update uniforms (no-op in this implementation)
     */
    void update_uniforms(const pose_type& render_pose) override { }

protected:
    /**
     * @brief Determines if the current iteration should be skipped
     */
    skip_option _p_should_skip() override {
        return threadloop::_p_should_skip();
    }

    /**
     * @brief Main processing loop for frame encoding and transmission
     *
     * This method:
     * 1. Captures the latest rendered frame
     * 2. Encodes it using hardware acceleration
     * 3. Transmits it to the client
     * 4. Tracks performance metrics
     */
    void _p_one_iteration() override {
        // Skip if no new frame is available
        if (buffer_pool == nullptr || buffer_pool->latest_decoded_image == -1) {
            log->info("no decoded image, returning");
            return;
        }

        // Record timing for performance analysis
        auto acquire_image_start_time = std::chrono::high_resolution_clock::now();

        // Acquire the latest frame and pose data
        std::pair<ILLIXR::vulkan::image_index_t, fast_pose_type> res =
            buffer_pool->post_processing_acquire_image(last_frame_ind);
        auto acquire_image_end_time = std::chrono::high_resolution_clock::now();

        auto ind  = res.first;
        auto pose = res.second;

        if (ind == -1) {
            return;
        }
        last_frame_ind = ind;

        // Record copy operation timing
        auto copy_start_time = std::chrono::high_resolution_clock::now();

        // Process color frames for both eyes
        for (auto eye = 0; eye < 2; eye++) {
            // Transfer color frame data to encoding buffer
            auto ret = av_hwframe_transfer_data(encode_src_color_frames[eye], avvk_color_frames[ind][eye].frame, 0);
            AV_ASSERT_SUCCESS(ret);
            encode_src_color_frames[eye]->pts = frame_count++;

            // Process depth frame if enabled
            if (pass_depth) {
                ret = av_hwframe_transfer_data(encode_src_depth_frames[eye], avvk_depth_frames[ind][eye].frame, 0);
                AV_ASSERT_SUCCESS(ret);
                encode_src_depth_frames[eye]->pts = frame_count++;
            }
        }

        auto copy_end_time = std::chrono::high_resolution_clock::now();
        buffer_pool->post_processing_release_image(ind);

        // Record encode operation timing
        auto encode_start_time = std::chrono::high_resolution_clock::now();

        // Encode frames for both eyes
        for (auto eye = 0; eye < 2; eye++) {
            // Encode color frame
            auto ret = avcodec_send_frame(codec_color_ctx, encode_src_color_frames[eye]);
            if (ret == AVERROR(EAGAIN)) {
                throw std::runtime_error{"FFmpeg encoder returned EAGAIN. Internal buffer full? Try using a higher-end GPU."};
            }
            AV_ASSERT_SUCCESS(ret);

            // Encode depth frame if enabled
            if (pass_depth) {
                ret = avcodec_send_frame(codec_depth_ctx, encode_src_depth_frames[eye]);
                if (ret == AVERROR(EAGAIN)) {
                    throw std::runtime_error{
                        "FFmpeg encoder returned EAGAIN. Internal buffer full? Try using a higher-end GPU."};
                }
                AV_ASSERT_SUCCESS(ret);
            }
        }

        // Receive encoded packets
        for (auto eye = 0; eye < 2; eye++) {
            // Receive color packet
            auto ret = avcodec_receive_packet(codec_color_ctx, encode_out_color_packets[eye]);
            if (ret == AVERROR(EAGAIN)) {
                throw std::runtime_error{"FFmpeg encoder returned EAGAIN when receiving packets. This should never happen."};
            }
            AV_ASSERT_SUCCESS(ret);

            // Receive depth packet if enabled
            if (pass_depth) {
                ret = avcodec_receive_packet(codec_depth_ctx, encode_out_depth_packets[eye]);
                if (ret == AVERROR(EAGAIN)) {
                    throw std::runtime_error{
                        "FFmpeg encoder returned EAGAIN when receiving packets. This should never happen."};
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
        metrics["copy_time"] += copy_time;
        metrics["encode_time"] += encode_time;
        metrics["acquire_image_time"] += acquire_image_time;

        // Send encoded frame to client
        enqueue_for_network_send(pose);

        // Log performance metrics every second
        if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - fps_start_time)
                .count() >= 1) {
            log->info("Encoder FPS: {}", fps_counter);
            fps_start_time = std::chrono::high_resolution_clock::now();
            log->warn("{},{}", fps_start_time.time_since_epoch().count(), fps_counter);

            for (auto& metric : metrics) {
                double fps = std::max(fps_counter, (double) 0);
                log->info("{}: {}", metric.first, metric.second / fps);
                metric.second = 0;
            }

            if (pass_depth) {
                log->info("Depth frame sizes - Left: {} Right: {}", encode_out_depth_packets[0]->size,
                          encode_out_depth_packets[1]->size);
                // std::cout << "depth left: " << encode_out_depth_packets[0]->size << " depth right: " <<
                // encode_out_depth_packets[0]->size << std::endl;
            }

            fps_counter = 0;
        } else {
            fps_counter++;
        }
    }

private:
    /**
     * @brief Sends encoded frame data to the client
     * @param pose The pose data associated with the frame
     */
    void enqueue_for_network_send(fast_pose_type& pose) {
        uint64_t timestamp =
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch())
                .count();

        if (pass_depth) {
            frames_topic.put(std::make_shared<compressed_frame>(encode_out_color_packets[0], encode_out_color_packets[1],
                                                                encode_out_depth_packets[0], encode_out_depth_packets[1], pose,
                                                                timestamp, nalu_only));
        } else {
            frames_topic.put(std::make_shared<compressed_frame>(encode_out_color_packets[0], encode_out_color_packets[1], pose,
                                                                timestamp, nalu_only));
        }
    }

    std::shared_ptr<spdlog::logger>                      log;
    std::shared_ptr<vulkan::display_provider>            dp;
    std::shared_ptr<switchboard>                         sb;
    switchboard::network_writer<compressed_frame>        frames_topic;
    switchboard::reader<fast_pose_type>                  render_pose;
    std::shared_ptr<vulkan::buffer_pool<fast_pose_type>> buffer_pool;
    std::vector<std::array<ffmpeg_vk_frame, 2>>          avvk_color_frames;
    std::vector<std::array<ffmpeg_vk_frame, 2>>          avvk_depth_frames;

    int  framerate = 144;
    long bitrate   = OFFLOAD_RENDERING_BITRATE;

    bool pass_depth = false;
    bool nalu_only  = false;

    AVBufferRef* device_ctx;
    AVBufferRef* cuda_device_ctx;
    AVBufferRef* frame_ctx;
    AVBufferRef* cuda_frame_ctx;

    AVCodecContext*          codec_color_ctx;
    std::array<AVFrame*, 2>  encode_src_color_frames;
    std::array<AVPacket*, 2> encode_out_color_packets;

    AVCodecContext*          codec_depth_ctx;
    std::array<AVFrame*, 2>  encode_src_depth_frames;
    std::array<AVPacket*, 2> encode_out_depth_packets;

    uint64_t frame_count = 0;

    double                                         fps_counter    = 0;
    std::chrono::high_resolution_clock::time_point fps_start_time = std::chrono::high_resolution_clock::now();
    std::map<std::string, uint32_t>                metrics;

    uint16_t last_frame_ind = -1;

    std::atomic<bool> ready{false};

    /**
     * @brief Initializes the FFmpeg Vulkan device context
     *
     * Sets up the Vulkan device context for FFmpeg hardware acceleration,
     * configuring queues and extensions for optimal performance.
     */
    void ffmpeg_init_device() {
        this->device_ctx      = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_VULKAN);
        auto hwdev_ctx        = reinterpret_cast<AVHWDeviceContext*>(device_ctx->data);
        auto vulkan_hwdev_ctx = reinterpret_cast<AVVulkanDeviceContext*>(hwdev_ctx->hwctx);

        // Configure Vulkan device context
        vulkan_hwdev_ctx->inst            = dp->vk_instance;
        vulkan_hwdev_ctx->phys_dev        = dp->vk_physical_device;
        vulkan_hwdev_ctx->act_dev         = dp->vk_device;
        vulkan_hwdev_ctx->device_features = dp->features;

        // Configure queue families for different operations
        for (auto& queue : dp->queues) {
            switch (queue.first) {
            case vulkan::queue::GRAPHICS:
                vulkan_hwdev_ctx->queue_family_index    = queue.second.family;
                vulkan_hwdev_ctx->nb_graphics_queues    = 1;
                vulkan_hwdev_ctx->queue_family_tx_index = queue.second.family;
                vulkan_hwdev_ctx->nb_tx_queues          = 1;
                // TODO: data race here! need to supply the lock_queue and unlock_queue function.
                // Not yet available in release version of ffmpeg
                break;
            case vulkan::queue::COMPUTE:
                vulkan_hwdev_ctx->queue_family_comp_index = queue.second.family;
                vulkan_hwdev_ctx->nb_comp_queues          = 1;
            default:
                break;
            }
        }

        // Configure dedicated transfer queue if available
        if (dp->queues.find(vulkan::queue::DEDICATED_TRANSFER) != dp->queues.end()) {
            vulkan_hwdev_ctx->queue_family_tx_index = dp->queues[vulkan::queue::DEDICATED_TRANSFER].family;
            vulkan_hwdev_ctx->nb_tx_queues          = 1;
        }

        // Vulkan Video not used in current implementation
        vulkan_hwdev_ctx->nb_encode_queues          = 0;
        vulkan_hwdev_ctx->nb_decode_queues          = 0;
        vulkan_hwdev_ctx->queue_family_encode_index = -1;
        vulkan_hwdev_ctx->queue_family_decode_index = -1;

        vulkan_hwdev_ctx->alloc         = nullptr;
        vulkan_hwdev_ctx->get_proc_addr = vkGetInstanceProcAddr;

        // Configure Vulkan extensions
        vulkan_hwdev_ctx->enabled_inst_extensions    = dp->enabled_instance_extensions.data();
        vulkan_hwdev_ctx->nb_enabled_inst_extensions = dp->enabled_instance_extensions.size();
        vulkan_hwdev_ctx->enabled_dev_extensions     = dp->enabled_device_extensions.data();
        vulkan_hwdev_ctx->nb_enabled_dev_extensions  = dp->enabled_device_extensions.size();

        vulkan_hwdev_ctx->lock_queue   = &ffmpeg_lock_queue;
        vulkan_hwdev_ctx->unlock_queue = &ffmpeg_unlock_queue;

        AV_ASSERT_SUCCESS(av_hwdevice_ctx_init(device_ctx));
        log->info("FFmpeg Vulkan hwdevice context initialized");
    }

    /**
     * @brief Initializes the FFmpeg CUDA device context for hardware acceleration
     */
    void ffmpeg_init_cuda_device() {
        auto ret = av_hwdevice_ctx_create(&cuda_device_ctx, AV_HWDEVICE_TYPE_CUDA, nullptr, nullptr, 0);
        AV_ASSERT_SUCCESS(ret);
        if (cuda_device_ctx == nullptr) {
            throw std::runtime_error{"Failed to create FFmpeg CUDA hwdevice context"};
        }
        log->info("FFmpeg CUDA hwdevice context initialized");
    }

    /**
     * @brief Initializes the FFmpeg frame context for Vulkan frames
     *
     * Sets up the frame context with appropriate pixel format and dimensions
     * for hardware-accelerated frame processing.
     */
    void ffmpeg_init_frame_ctx() {
        assert(this->buffer_pool != nullptr);
        this->frame_ctx = av_hwframe_ctx_alloc(device_ctx);
        if (!frame_ctx) {
            throw std::runtime_error{"Failed to create FFmpeg Vulkan hwframe context"};
        }

        auto hwframe_ctx    = reinterpret_cast<AVHWFramesContext*>(frame_ctx->data);
        hwframe_ctx->format = AV_PIX_FMT_VULKAN;

        // Configure pixel format based on Vulkan image format
        auto pix_format = vulkan::ffmpeg_utils::get_pix_format_from_vk_format(buffer_pool->image_pool[0][0].image_info.format);
        if (!pix_format) {
            throw std::runtime_error{"Unsupported Vulkan image format when creating FFmpeg Vulkan hwframe context"};
        }
        assert(pix_format == AV_PIX_FMT_BGRA);

        // Set frame properties
        hwframe_ctx->sw_format         = AV_PIX_FMT_BGRA;
        hwframe_ctx->width             = buffer_pool->image_pool[0][0].image_info.extent.width;
        hwframe_ctx->height            = buffer_pool->image_pool[0][0].image_info.extent.height;
        hwframe_ctx->initial_pool_size = 0;

        auto ret = av_hwframe_ctx_init(frame_ctx);
        AV_ASSERT_SUCCESS(ret);
    }

    /**
     * @brief Initializes the FFmpeg CUDA frame context
     *
     * Sets up the CUDA frame context for hardware-accelerated encoding,
     * configuring frame dimensions and format to match the Vulkan frames.
     */
    void ffmpeg_init_cuda_frame_ctx() {
        assert(this->buffer_pool != nullptr);
        auto cuda_frame_ref = av_hwframe_ctx_alloc(cuda_device_ctx);
        if (!cuda_frame_ref) {
            throw std::runtime_error{"Failed to create FFmpeg CUDA hwframe context"};
        }

        // Configure CUDA frame properties
        auto cuda_hwframe_ctx       = reinterpret_cast<AVHWFramesContext*>(cuda_frame_ref->data);
        cuda_hwframe_ctx->format    = AV_PIX_FMT_CUDA;
        cuda_hwframe_ctx->sw_format = AV_PIX_FMT_BGRA;
        cuda_hwframe_ctx->width     = buffer_pool->image_pool[0][0].image_info.extent.width;
        cuda_hwframe_ctx->height    = buffer_pool->image_pool[0][0].image_info.extent.height;

        auto ret = av_hwframe_ctx_init(cuda_frame_ref);
        AV_ASSERT_SUCCESS(ret);
        this->cuda_frame_ctx = cuda_frame_ref;
    }

    /**
     * @brief Initializes the frame buffer pool for both color and depth frames
     *
     * Creates and configures AVFrame objects for both color and depth frames,
     * setting up the necessary Vulkan and CUDA resources for hardware acceleration.
     */
    void ffmpeg_init_buffer_pool() {
        assert(this->buffer_pool != nullptr);

        // Initialize color frame arrays
        avvk_color_frames.resize(buffer_pool->image_pool.size());
        if (pass_depth) {
            avvk_depth_frames.resize(buffer_pool->depth_image_pool.size());
        }

        // Set up frames for each buffer in the pool
        for (size_t i = 0; i < buffer_pool->image_pool.size(); i++) {
            for (size_t eye = 0; eye < 2; eye++) {
                // Create and configure color frame
                auto vk_frame = av_vk_frame_alloc();
                if (!vk_frame) {
                    throw std::runtime_error{"Failed to allocate FFmpeg Vulkan frame for color image"};
                }

                // Configure Vulkan frame properties
                vk_frame->img[0]          = buffer_pool->image_pool[i][eye].image;
                vk_frame->tiling          = buffer_pool->image_pool[i][eye].image_info.tiling;
                vk_frame->mem[0]          = buffer_pool->image_pool[i][eye].allocation_info.deviceMemory;
                vk_frame->size[0]         = buffer_pool->image_pool[i][eye].allocation_info.size;
                vk_frame->offset[0]       = buffer_pool->image_pool[i][eye].allocation_info.offset;
                vk_frame->queue_family[0] = dp->queues[vulkan::queue::GRAPHICS].family;

                // Create and configure semaphore for synchronization
                VkExportSemaphoreCreateInfo export_semaphore_create_info{
                    VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO, nullptr, VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT};
                vk_frame->sem[0]       = vulkan::create_timeline_semaphore(dp->vk_device, 0, &export_semaphore_create_info);
                vk_frame->sem_value[0] = 0;
                vk_frame->layout[0]    = VK_IMAGE_LAYOUT_UNDEFINED;

                avvk_color_frames[i][eye].vk_frame = vk_frame;

                // Create and configure AVFrame for color
                auto av_frame = av_frame_alloc();
                if (!av_frame) {
                    throw std::runtime_error{"Failed to allocate FFmpeg frame for color image"};
                }
                av_frame->format                = AV_PIX_FMT_VULKAN;
                av_frame->width                 = buffer_pool->image_pool[i][eye].image_info.extent.width;
                av_frame->height                = buffer_pool->image_pool[i][eye].image_info.extent.height;
                av_frame->hw_frames_ctx         = av_buffer_ref(frame_ctx);
                av_frame->data[0]               = reinterpret_cast<uint8_t*>(vk_frame);
                av_frame->buf[0]                = av_buffer_create(av_frame->data[0], 0, [](void*, uint8_t*) { }, nullptr, 0);
                av_frame->pts                   = 0;
                avvk_color_frames[i][eye].frame = av_frame;

                // Set up depth frame if enabled
                if (pass_depth) {
                    auto vk_depth_frame = av_vk_frame_alloc();
                    if (!vk_depth_frame) {
                        throw std::runtime_error{"Failed to allocate FFmpeg Vulkan frame for depth image"};
                    }

                    // Configure Vulkan depth frame properties
                    vk_depth_frame->img[0]          = buffer_pool->depth_image_pool[i][eye].image;
                    vk_depth_frame->tiling          = buffer_pool->depth_image_pool[i][eye].image_info.tiling;
                    vk_depth_frame->mem[0]          = buffer_pool->depth_image_pool[i][eye].allocation_info.deviceMemory;
                    vk_depth_frame->size[0]         = buffer_pool->depth_image_pool[i][eye].allocation_info.size;
                    vk_depth_frame->offset[0]       = buffer_pool->depth_image_pool[i][eye].allocation_info.offset;
                    vk_depth_frame->queue_family[0] = dp->queues[vulkan::queue::GRAPHICS].family;

                    vk_depth_frame->sem[0] = vulkan::create_timeline_semaphore(dp->vk_device, 0, &export_semaphore_create_info);
                    vk_depth_frame->sem_value[0] = 0;
                    vk_depth_frame->layout[0]    = VK_IMAGE_LAYOUT_UNDEFINED;

                    avvk_depth_frames[i][eye].vk_frame = vk_depth_frame;

                    // Create and configure AVFrame for depth
                    auto av_depth_frame = av_frame_alloc();
                    if (!av_depth_frame) {
                        throw std::runtime_error{"Failed to allocate FFmpeg frame for depth image"};
                    }
                    av_depth_frame->format        = AV_PIX_FMT_VULKAN;
                    av_depth_frame->width         = buffer_pool->depth_image_pool[i][eye].image_info.extent.width;
                    av_depth_frame->height        = buffer_pool->depth_image_pool[i][eye].image_info.extent.height;
                    av_depth_frame->hw_frames_ctx = av_buffer_ref(frame_ctx);
                    av_depth_frame->data[0]       = reinterpret_cast<uint8_t*>(vk_depth_frame);
                    av_depth_frame->buf[0] = av_buffer_create(av_depth_frame->data[0], 0, [](void*, uint8_t*) { }, nullptr, 0);
                    av_depth_frame->pts    = 0;
                    avvk_depth_frames[i][eye].frame = av_depth_frame;
                }
            }
        }

        // Initialize source frames for encoding
        for (size_t eye = 0; eye < 2; eye++) {
            // Set up color source frame
            encode_src_color_frames[eye]                = av_frame_alloc();
            encode_src_color_frames[eye]->format        = AV_PIX_FMT_CUDA;
            encode_src_color_frames[eye]->hw_frames_ctx = av_buffer_ref(cuda_frame_ctx);
            encode_src_color_frames[eye]->width         = buffer_pool->image_pool[0][0].image_info.extent.width;
            encode_src_color_frames[eye]->height        = buffer_pool->image_pool[0][0].image_info.extent.height;

            // Configure color space properties
            encode_src_color_frames[eye]->color_range     = AVCOL_RANGE_JPEG;
            encode_src_color_frames[eye]->colorspace      = AVCOL_SPC_BT709;
            encode_src_color_frames[eye]->color_trc       = AVCOL_TRC_BT709;
            encode_src_color_frames[eye]->color_primaries = AVCOL_PRI_BT709;
            encode_src_color_frames[eye]->pict_type       = AV_PICTURE_TYPE_I;

            auto ret = av_hwframe_get_buffer(cuda_frame_ctx, encode_src_color_frames[eye], 0);
            AV_ASSERT_SUCCESS(ret);

            // Set up depth source frame if enabled
            if (pass_depth) {
                encode_src_depth_frames[eye]                = av_frame_alloc();
                encode_src_depth_frames[eye]->format        = AV_PIX_FMT_CUDA;
                encode_src_depth_frames[eye]->hw_frames_ctx = av_buffer_ref(cuda_frame_ctx);
                encode_src_depth_frames[eye]->width         = buffer_pool->depth_image_pool[0][0].image_info.extent.width;
                encode_src_depth_frames[eye]->height        = buffer_pool->depth_image_pool[0][0].image_info.extent.height;

                // Configure depth frame color space
                encode_src_depth_frames[eye]->color_range     = AVCOL_RANGE_JPEG;
                encode_src_depth_frames[eye]->colorspace      = AVCOL_SPC_BT709;
                encode_src_depth_frames[eye]->color_trc       = AVCOL_TRC_BT709;
                encode_src_depth_frames[eye]->color_primaries = AVCOL_PRI_BT709;
                encode_src_depth_frames[eye]->pict_type       = AV_PICTURE_TYPE_I;

                ret = av_hwframe_get_buffer(cuda_frame_ctx, encode_src_depth_frames[eye], 0);
                AV_ASSERT_SUCCESS(ret);
            }
        }
    }

    /**
     * @brief Initializes the FFmpeg encoders for color and depth frames
     *
     * Sets up hardware-accelerated encoders with optimal settings for low-latency
     * streaming of VR content. Configures separate encoders for color and depth
     * frames if depth transmission is enabled.
     */
    void ffmpeg_init_encoder() {
        // Find hardware-accelerated encoder
        auto encoder = avcodec_find_encoder_by_name(OFFLOAD_RENDERING_FFMPEG_ENCODER_NAME);
        if (!encoder) {
            throw std::runtime_error{"Failed to find FFmpeg encoder"};
        }

        // Initialize color encoder
        this->codec_color_ctx = avcodec_alloc_context3(encoder);
        if (!codec_color_ctx) {
            throw std::runtime_error{"Failed to allocate FFmpeg encoder context for color images"};
        }

        // Configure multithreading
        codec_color_ctx->thread_count = 0; // auto
        codec_color_ctx->thread_type  = FF_THREAD_SLICE;

        // Configure pixel format and hardware acceleration
        codec_color_ctx->pix_fmt       = AV_PIX_FMT_CUDA;
        codec_color_ctx->sw_pix_fmt    = AV_PIX_FMT_BGRA;
        codec_color_ctx->hw_frames_ctx = av_buffer_ref(cuda_frame_ctx);

        // Set frame dimensions and timing
        codec_color_ctx->width     = buffer_pool->image_pool[0][0].image_info.extent.width;
        codec_color_ctx->height    = buffer_pool->image_pool[0][0].image_info.extent.height;
        codec_color_ctx->time_base = {1, framerate};
        codec_color_ctx->framerate = {framerate, 1};
        codec_color_ctx->bit_rate  = bitrate;

        // Configure color space
        codec_color_ctx->color_range     = AVCOL_RANGE_JPEG;
        codec_color_ctx->colorspace      = AVCOL_SPC_BT709;
        codec_color_ctx->color_trc       = AVCOL_TRC_BT709;
        codec_color_ctx->color_primaries = AVCOL_PRI_BT709;

        // Configure for low latency
        codec_color_ctx->max_b_frames = 0;
        codec_color_ctx->gop_size     = 15; // Intra-frame interval
        av_opt_set_int(codec_color_ctx->priv_data, "zerolatency", 1, 0);
        av_opt_set_int(codec_color_ctx->priv_data, "delay", 0, 0);

        auto ret = avcodec_open2(codec_color_ctx, encoder, nullptr);
        AV_ASSERT_SUCCESS(ret);

        // Initialize depth encoder if enabled
        if (pass_depth) {
            this->codec_depth_ctx = avcodec_alloc_context3(encoder);
            if (!codec_depth_ctx) {
                throw std::runtime_error{"Failed to allocate FFmpeg encoder context for depth images"};
            }

            // Configure multithreading
            codec_depth_ctx->thread_count = 0;
            codec_depth_ctx->thread_type  = FF_THREAD_SLICE;

            // Configure pixel format and hardware acceleration
            codec_depth_ctx->pix_fmt       = AV_PIX_FMT_CUDA;
            codec_depth_ctx->sw_pix_fmt    = AV_PIX_FMT_BGRA;
            codec_depth_ctx->hw_frames_ctx = av_buffer_ref(cuda_frame_ctx);

            // Set frame dimensions and timing
            codec_depth_ctx->width     = buffer_pool->depth_image_pool[0][0].image_info.extent.width;
            codec_depth_ctx->height    = buffer_pool->depth_image_pool[0][0].image_info.extent.height;
            codec_depth_ctx->time_base = {1, framerate};
            codec_depth_ctx->framerate = {framerate, 1};
            codec_depth_ctx->bit_rate  = bitrate;

            // Configure color space
            codec_depth_ctx->color_range     = AVCOL_RANGE_JPEG;
            codec_depth_ctx->colorspace      = AVCOL_SPC_BT709;
            codec_depth_ctx->color_trc       = AVCOL_TRC_BT709;
            codec_depth_ctx->color_primaries = AVCOL_PRI_BT709;

            // Configure for low latency
            codec_depth_ctx->max_b_frames = 0;
            codec_depth_ctx->gop_size     = 15;
            av_opt_set_int(codec_depth_ctx->priv_data, "zerolatency", 1, 0);
            av_opt_set_int(codec_depth_ctx->priv_data, "delay", 0, 0);

            ret = avcodec_open2(codec_depth_ctx, encoder, nullptr);
            AV_ASSERT_SUCCESS(ret);
        }
    }
};

/**
 * @class offload_rendering_server_loader
 * @brief Plugin loader for the offload rendering server
 *
 * Handles plugin registration and Vulkan extension requirements for the
 * offload rendering server component.
 */
class offload_rendering_server_loader
    : public plugin
    , public vulkan::vk_extension_request {
public:
    /**
     * @brief Constructor registers the server plugin with the system
     * @param name Plugin name
     * @param pb Phonebook for component lookup
     */
    offload_rendering_server_loader(const std::string& name, phonebook* pb)
        : plugin(name, pb)
        , offload_rendering_server_plugin{std::make_shared<offload_rendering_server>(name, pb)} {
        pb->register_impl<vulkan::timewarp>(offload_rendering_server_plugin);
        pb->register_impl<pose_prediction>(offload_rendering_server_plugin);
        log->info("Registered vulkan::timewarp and pose_prediction implementations");
    }

    /**
     * @brief Get required Vulkan instance extensions
     * @return List of required instance extension names
     */
    std::vector<const char*> get_required_instance_extensions() override {
        return {VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME, VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME,
                VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME};
    }

    /**
     * @brief Get required Vulkan device extensions
     * @return List of required device extension names
     */
    std::vector<const char*> get_required_devices_extensions() override {
        return {VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME, VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
                VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME, VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
                VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME};
    }

    void start() override {
        offload_rendering_server_plugin->start();
    }

    void stop() override {
        offload_rendering_server_plugin->stop();
    }

private:
    std::shared_ptr<offload_rendering_server> offload_rendering_server_plugin;
    std::shared_ptr<spdlog::logger>           log{spdlogger("debug")};
};

PLUGIN_MAIN(offload_rendering_server_loader)
