#pragma once

/**
 * @class offload_rendering_client
 * @brief Main client implementation for offload rendering
 *
 * This class handles:
 * 1. Reception of encoded frames from the server
 * 2. Hardware-accelerated HEVC decoding using FFmpeg/CUDA (Linux) or EGL contexts (Android)
 * 3. Color space conversion (NV12 to RGBA)
 * 4. Vulkan image management and synchronization (Linux)
 * 5. Combined pose and hand tracking synchronization with the server
 *
 * The client supports two modes:
 * - Realtime mode: Receives and displays frames with real-time pose updates
 * - Comparison mode: Uses a fixed pose for image quality comparison
 *
 * Configuration is controlled through environment variables:
 * - ILLIXR_USE_DEPTH_IMAGES: Enable depth frame reception/decoding
 * - ILLIXR_USE_HAND_TRACKING: Enable hand tracking data transmission
 */
#define DOUBLE_INCLUDE
// ILLIXR core headers
#ifdef USING_OPENXR
#    include "illixr/data_format/poses/combined_pose.hpp"
#endif

#include "illixr/data_format/frame.hpp"
#include "illixr/data_format/latency_data.hpp"
#include "illixr/data_format/pose_prediction.hpp"
#include "illixr/data_format/serialization/frame.hpp"
#include "illixr/data_format/serialization/head_pose.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"
#undef DOUBLE_INCLUDE
#ifdef USING_OPENXR
#    include <mutex>
#    include <openxr/openxr.h>
#    include <thread>
#    ifdef __ANDROID__
#        include "android/stereo_surface_decoder.hpp"
#        include "illixr/quest3_params.hpp"

#        include <android_native_app_glue.h>
#    endif
#else
// ILLIXR Vulkan headers
#    include "illixr/vk/display_provider.hpp"
#    include "illixr/vk/ffmpeg_utils.hpp"
#    include "illixr/vk/render_pass.hpp"
#    include "illixr/vk/vk_extension_request.hpp"
#    include "illixr/vk/vulkan_utils.hpp"

// FFmpeg headers (C interface)
extern "C" {
#    include "libavfilter_illixr/buffersink.h"
#    include "libavfilter_illixr/buffersrc.h"
#    include "libswscale_illixr/swscale.h"
}

// NVIDIA nppi headers
#    include "nppi.h"
#endif

namespace ILLIXR {

class offload_rendering_client
    : public threadloop
#ifndef __ANDROID__
    , public vulkan::app
#endif
{
public:
    /**
     * @brief Constructor initializes the client with configuration from environment variables
     * @param name Plugin name
     * @param pb Phonebook for component lookup
     */
    offload_rendering_client(const std::string& name, phonebook* pb);

#ifdef __ANDROID__
    ~offload_rendering_client() override;
#else
    /**
     * @brief Start the client thread and initialize FFmpeg/CUDA resources
     */
    void start() override;

    /**
     * @brief Set up Vulkan resources and initialize frame buffers
     * @param render_pass The Vulkan render pass to use
     * @param subpass The subpass index
     * @param buffer_pool The buffer pool for frame data
     */
    void setup(VkRenderPass render_pass, uint32_t subpass,
               std::shared_ptr<vulkan::buffer_pool<data_format::pose::fast_head_pose_type>> buffer_pool) override;

    /**
     * @brief Record command buffer (no-op in this implementation)
     */
    void record_command_buffer(VkCommandBuffer commandBuffer, VkFramebuffer framebuffer, int buffer_ind, bool left) override {
        (void) commandBuffer;
        (void) framebuffer;
        (void) buffer_ind;
        (void) left;
    }

    /**
     * @brief Update uniforms (no-op in this implementation)
     */
    void update_uniforms(const data_format::pose::head_pose_type& render_pose) override {
        (void) render_pose;
    }

    /**
     * @brief Indicates this is an external renderer
     * @return true since this is an external renderer
     */
    bool is_external() override {
        return true;
    }

    /**
     * @brief Clean up resources on destruction
     */
    void destroy() override;
#endif // __ANDROID__

protected:
    /**
     * @brief Thread setup (no-op in this implementation)
     */
    void _p_thread_setup() override;

    /**
     * @brief Determines if the current iteration should be skipped
     */
    skip_option _p_should_skip() override {
        return threadloop::_p_should_skip();
    }

#ifndef __ANDROID__
    [[maybe_unused]] void copy_image_to_cpu_and_save_file(AVFrame* frame);
    [[maybe_unused]] void save_nv12_img_to_png(AVFrame* cuda_frame) const;
    void transition_layout(VkCommandBuffer cmd_buf, AVFrame* frame, VkImageLayout old_layout, VkImageLayout new_layout);
#endif
    /**
     * @brief Main processing loop for frame decoding and display
     *
     * This method:
     * 1. Sends the latest pose and hand tracking data to the server
     * 2. Receives and decodes encoded frames
     * 3. Performs color space conversion
     * 4. Updates display buffers
     * 5. Tracks performance metrics
     */
    void _p_one_iteration() override;

private:
#ifdef __ANDROID__
    /**
     * @brief Metadata extracted from a received compressed_frame, shared
     *         between the receiver thread and _p_one_iteration.
     *
     * All fields belong to a specific server frame and are stored together
     * in frame_meta_map_ keyed by frame_number.  This ensures construct_dual_frames
     * retrieves the metadata that matches the frame the decoder actually finished,
     * rather than whichever frame arrived most recently from the network.
     */
    struct frame_meta {
        BUFFER_TYPE pose;
        uint64_t    frame_number{0};
        uint64_t    frame_time{0};
        uint64_t    pose_id{0};
        float       near_z{0.f};
        float       far_z{0.f};
        double      encode_time{0.};
        bool        consumed{false};
    };

    /**
     * @brief Receiver thread: dequeues compressed frames from the network,
     *        drops frames when the decoder is backed up, queues encoded data
     *        to the hardware decoders, and updates frame_meta_map_.
     */
    void receiver_loop();

    /**
     * @brief Log Android decode timing statistics.
     *
     * Collects and logs timing statistics from color and depth decoders
     * including queue time, decode latency, and texture update time.
     */
    void log_android_decode_timing();

    /**
     * @brief Construct a complete dual_frames with both color and depth.
     * Must be called from thread with GL context current.
     * @param render_time Current render timestamp
     * @return Complete dual_frames struct with color and depth populated
     */
    data_format::dual_frames construct_dual_frames(time_point render_time);

#else
    /**
     * @brief Send the latest pose to the server
     */
    void push_pose();

    /**
     * @brief Receive and process network data
     * @return true if data was received successfully, false otherwise
     */
    bool network_receive();

    [[maybe_unused]] void submit_command_buffer(VkCommandBuffer vk_command_buffer);

    /**
     * @brief Initialize FFmpeg Vulkan device context
     *
     * Sets up the FFmpeg Vulkan device context with the appropriate queues_,
     * features, and extensions required for hardware-accelerated decoding.
     */
    void ffmpeg_init_device();

    /**
     * @brief Initialize FFmpeg CUDA device context
     *
     * Creates and initializes the CUDA hardware device context for FFmpeg.
     */
    void ffmpeg_init_cuda_device();

    /**
     * @brief Initialize FFmpeg frame context for Vulkan
     *
     * Sets up the frame context for Vulkan image handling, configuring
     * the pixel format and dimensions based on the buffer pool settings.
     */
    void ffmpeg_init_frame_ctx();

    /**
     * @brief Create CUDA frame context for a specific pixel format
     * @param fmt The desired pixel format
     * @return AVBufferRef* The created frame context
     */
    AVBufferRef* create_cuda_frame_ctx(AVPixelFormat fmt);

    /**
     * @brief Initialize CUDA frame contexts for NV12 and BGRA formats
     */
    void ffmpeg_init_cuda_frame_ctx();

    /**
     * @brief Initialize FFmpeg buffer pool and frame resources
     *
     * Sets up the buffer pool for both color and depth frames, including:
     * - AVVkFrame creation and configuration
     * - AVFrame allocation and setup
     * - Command buffer creation for layout transitions
     * - NPP buffer allocation for color space conversion
     */
    void ffmpeg_init_buffer_pool();

    /**
     * @brief Initialize FFmpeg decoder
     *
     * Sets up the HEVC decoder with CUDA hardware acceleration for both color
     * and depth frames (if enabled). Configures decoder parameters for optimal
     * low-latency decoding.
     */
    void ffmpeg_init_decoder();
#endif // __ANDROID__

    std::shared_ptr<switchboard>    switchboard_;
    std::shared_ptr<spdlog::logger> log_;
#ifdef __ANDROID__
    switchboard::writer<data_format::dual_frames> frame_writer_;
#else
    std::shared_ptr<vulkan::display_provider> display_provider_;
#endif
    switchboard::buffered_reader<data_format::compressed_frame> frames_reader_;
    switchboard::reader<data_format::network_latency_result>    network_latency_reader_;

#ifndef USING_OPENXR
    // Pose transmission to server
    switchboard::network_writer<data_format::pose::fast_head_pose_type> pose_writer_;
    std::shared_ptr<data_format::pose_prediction>                       pose_prediction_;
#endif
    std::atomic<bool>               ready_ = false;
    std::shared_ptr<relative_clock> clock_;

#ifndef __ANDROID__
    std::shared_ptr<vulkan::buffer_pool<data_format::pose::fast_head_pose_type>> buffer_pool_;
#endif
    bool use_depth_ = false;

#ifdef __ANDROID__

    bool use_motion_vectors_ = false;

    // Receiver thread: feeds encoded data to decoders independently of the
    // consumer (_p_one_iteration) so decoder latency cannot block reception.
    std::thread       receiver_thread_;
    std::atomic<bool> receiver_running_{false};
    // Maximum number of encoded frames allowed in the color decoder input
    // queue before incoming frames are dropped to prevent growing latency.
    // At 90fps a frame arrives every ~11.1ms; the feeder thread's
    // dequeueInputBuffer has a 2ms timeout and may loop several times per
    // frame under normal scheduling jitter.  A threshold of 3 fires on any
    // brief stall; 8 gives ~89ms of headroom while still bounding latency
    // to under 200ms if the decoder genuinely falls behind.
    static constexpr size_t MAX_DECODER_QUEUE_DEPTH = 8;

    // Maps server frame_number -> complete frame metadata for that frame.
    // Written by receiver_thread_ as each compressed_frame arrives.
    // Read and pruned by _p_one_iteration once the decoded frame_number is known.
    // Entries for frame numbers older than the one just consumed are erased at
    // the same time, so the map stays bounded even if the decoder skips frames.
    std::map<uint64_t, frame_meta> frame_meta_map_;
    std::mutex                     frame_meta_map_mutex_;

    android_app* app_;

    std::unique_ptr<stereo_surface_decoder> color_decoder_;
    std::unique_ptr<stereo_surface_decoder> depth_decoder_;
    // Motion-vector decoder (432x432 HEVC 10-bit)
    std::unique_ptr<stereo_surface_decoder> motion_vec_decoder_;

    // Android timing metrics (accumulated between reports)
    // uint64_t android_queue_time_us_{0};         // Time spent queueing data to decoders
    // uint64_t android_texture_update_time_us_{0}; // Time spent updating GPU textures
    // uint64_t android_total_frame_time_us_{0};    // Total end-to-end frame processing time
    // uint64_t android_timing_frame_count_{0};     // Number of frames in current timing window

#else
    std::vector<std::array<vulkan::ffmpeg_utils::ffmpeg_vk_frame, 2>> avvk_color_frames_;
    std::vector<std::array<vulkan::ffmpeg_utils::ffmpeg_vk_frame, 2>> avvk_depth_frames_;
    std::vector<std::array<VkCommandBuffer, 2>>                       layout_transition_start_cmd_bufs_;
    std::vector<std::array<VkCommandBuffer, 2>>                       layout_transition_end_cmd_bufs_;
    AVBufferRef*                                                      device_ctx_          = nullptr;
    AVBufferRef*                                                      cuda_device_ctx_     = nullptr;
    AVBufferRef*                                                      frame_ctx_           = nullptr;
    AVBufferRef*                                                      cuda_nv12_frame_ctx_ = nullptr;
    AVBufferRef*                                                      cuda_bgra_frame_ctx_ = nullptr;

    AVCodecContext*          codec_color_ctx_               = nullptr;
    std::array<AVPacket*, 2> decode_src_color_packets_      = {nullptr, nullptr};
    std::array<AVFrame*, 2>  decode_out_color_frames_       = {nullptr, nullptr};
    std::array<AVFrame*, 2>  decode_converted_color_frames_ = {nullptr, nullptr};

    AVCodecContext*          codec_depth_ctx_               = nullptr;
    std::array<AVPacket*, 2> decode_src_depth_packets_      = {nullptr, nullptr};
    std::array<AVFrame*, 2>  decode_out_depth_frames_       = {nullptr, nullptr};
    std::array<AVFrame*, 2>  decode_converted_depth_frames_ = {nullptr, nullptr};

    data_format::pose::fast_head_pose_type decoded_frame_pose_;

    VkCommandPool    command_pool{};
    Npp8u*           yuv420_y_plane_ = nullptr;
    Npp8u*           yuv420_u_plane_ = nullptr;
    Npp8u*           yuv420_v_plane_ = nullptr;
    int              y_step_         = 0;
    int              u_step_         = 0;
    int              v_step_         = 0;
    NppStreamContext npp_ctx_        = {};
#endif // __ANDROID__

    uint64_t frame_count_ = 0;

#ifndef __ANDROID__
    VkFence fence_{};
#endif

    uint16_t                                       fps_counter_    = 0;
    std::chrono::high_resolution_clock::time_point fps_start_time_ = std::chrono::high_resolution_clock::now();
    std::map<std::string, uint32_t>                metrics_{};
#ifdef USING_OPENXR
    std::array<float, 2> cached_fov_left_  = {0.0f, 0.0f};
    std::array<float, 2> cached_fov_right_ = {0.0f, 0.0f};
    std::array<float, 2> cached_fov_up_    = {0.0f, 0.0f};
    std::array<float, 2> cached_fov_down_  = {0.0f, 0.0f};
    bool                 fov_cached_       = false;
#endif
    uint64_t last_submitted_frame_{0};
};

} // namespace ILLIXR
