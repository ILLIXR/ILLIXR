#pragma once
#define MONADO_IS_SOURCE
#define DOUBLE_INCLUDE

#include "drivers/illixr/illixr_framebuffer.h"
#ifdef USING_OPENXR
    #include "illixr/data_format/poses/combined_pose.hpp"
#endif
#include "illixr/data_format/frame.hpp"
#include "illixr/data_format/pose_id.hpp"
#include "illixr/data_format/pose_prediction.hpp"
#include "illixr/data_format/serialization/head_pose.hpp"
#include "illixr/data_format/serialization/frame.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"
#include "illixr/vk/display_provider.hpp"
#include "illixr/vk/render_pass.hpp"
#include "illixr/vk/vulkan_utils.hpp"
#include "pose_relay.hpp"

#ifdef NVENC_ENCODER
    #include "nvenc/nvenc_encoder.hpp"
    #define OFFLOAD_RENDERING_BITRATE 100000000
#else
    #include "illixr/vk/ffmpeg_utils.hpp"
#endif

#undef DOUBLE_INCLUDE

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace ILLIXR {

/**
 * @class offload_rendering_server
 * @brief Main server implementation for offload rendering
 *
 * This class handles:
 * 1. Frame capture from the rendering pipeline
 * 2. Hardware-accelerated encoding using FFmpeg/CUDA or NVENC directly
 * 3. Network transmission of encoded frames
 * 4. Pose synchronization with the client
 * 5. Hand tracking data forwarding to Monado
 *
 * The server receives combined pose_with_hands data from the client,
 * extracts the head pose for rendering, and publishes hand tracking
 * data to the switchboard for Monado's ILLIXR driver to read.
 *
 * Compile with -DNVENC_ENCODER to use direct NVENC encoding without FFmpeg.
 */
class MY_EXPORT_API offload_rendering_server
    : public threadloop
    , public vulkan::timewarp
    , public data_format::pose_prediction
    , std::enable_shared_from_this<plugin> {
public:
    /**
     * @brief Constructor initializes the server with configuration from environment variables
     * @param name Plugin name
     * @param pb Phonebook for component lookup
     */
    offload_rendering_server(const std::string& name, phonebook* pb);
    void start() override;
    void stop() override;
    void _p_thread_setup() override;

    /**
     * @brief Sets up the rendering pipeline and encoding resources
     * @param render_pass Vulkan render pass handle
     * @param subpass Subpass index
     * @param _buffer_pool Buffer pool for frame management
     * @param input_texture_vulkan_coordinates Whether input textures use Vulkan coordinates
     */
    void setup(VkRenderPass render_pass, uint32_t subpass, std::shared_ptr<vulkan::buffer_pool<BUFFER_TYPE>> buffer_pool,
               bool input_texture_vulkan_coordinates, struct illixr_framebuffer* framebuffer_array, VkExtent2D extent) override;

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
    void destroy() override;

    /**
     * @brief Get the latest pose for rendering
     *
     * Extracts the head pose from the most recent pose_with_hands data
     * received from the client. Also forwards hand tracking data to the
     * switchboard for Monado (only once per new data arrival).
     */
    POSE_TYPE get_fast_pose() const override {
        return pose_relay_->get_pose();
    }

    /**
     * @brief Get predicted pose for a future time point (returns current pose)
     */
    POSE_TYPE get_fast_pose(POSE_TIME_TYPE future_time) const override {
        return pose_relay_->get_pose(future_time);
    }

    /**
     * @brief Check if fast pose data is reliable
     */
    bool fast_pose_reliable() const override {
        return pose_relay_->fast_pose_reliable();
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
    void set_offset(const Eigen::Quaternionf& orientation) override {
        (void) orientation;
    }

    /**
     * @brief Get orientation offset (returns identity in this implementation)
     */
    Eigen::Quaternionf get_offset() override {
        return {};
    }

    /**
     * @brief Correct pose data (no-op in this implementation)
     */
    data_format::pose::head_pose_type correct_pose(const data_format::pose::head_pose_type& pose) const override {
        (void) pose;
        return {};
    }

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
    void update_uniforms(const BUFFER_TYPE& r_pose) override {
        (void) r_pose;
    }

protected:
    /**
     * @brief Determines if the current iteration should be skipped
     */
    threadloop::skip_option _p_should_skip() override {
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
    void _p_one_iteration() override;

private:
    /**
     * @brief Sends encoded frame data to the client
     * @param pose The pose data associated with the frame
     */
    void enqueue_for_network_send(BUFFER_TYPE& pose, uint64_t pose_id);

#ifdef NVENC_ENCODER
    // ========================================================================
    // NVENC-specific methods (no FFmpeg dependency)
    // ========================================================================

    /**
     * @brief Initialize Vulkan context for CUDA-Vulkan interop
     */
    void nvenc_init_vulkan_context();

    /**
     * @brief Initialize NVENC encoders for color (and optionally depth)
     */
    void nvenc_init_encoders();

    /**
     * @brief Import buffer pool images into NVENC encoders
     */
    void nvenc_import_buffer_pool_images();

    /**
     * @brief Encode frames using NVENC
     * @param ind Buffer index to encode
     */
    void nvenc_encode_frames(int ind);

#else
    // ========================================================================
    // FFmpeg-specific methods
    // ========================================================================

    /**
     * @brief Initializes the FFmpeg Vulkan device context
     *
     * Sets up the Vulkan device context for FFmpeg hardware acceleration,
     * configuring queues and extensions for optimal performance.
     */
    void ffmpeg_init_device();

    /**
     * @brief Initializes the FFmpeg CUDA device context for hardware acceleration
     */
    void ffmpeg_init_cuda_device();

    /**
     * @brief Initializes the FFmpeg frame context for Vulkan frames
     *
     * Sets up the frame context with appropriate pixel format and dimensions
     * for hardware-accelerated frame processing.
     */
    void ffmpeg_init_frame_ctx();

    /**
     * @brief Initializes the FFmpeg CUDA frame context
     *
     * Sets up the CUDA frame context for hardware-accelerated encoding,
     * configuring frame dimensions and format to match the Vulkan frames.
     */
    void ffmpeg_init_cuda_frame_ctx();

    /**
     * @brief Initializes the frame buffer pool for both color and depth frames
     *
     * Creates and configures AVFrame objects for both color and depth frames,
     * setting up the necessary Vulkan and CUDA resources for hardware acceleration.
     */
    void ffmpeg_init_buffer_pool();

    /**
     * @brief Initializes the FFmpeg encoders for color and depth frames
     *
     * Sets up hardware-accelerated encoders with optimal settings for low-latency
     * streaming of VR content. Configures separate encoders for color and depth
     * frames if depth transmission is enabled.
     */
    void ffmpeg_init_encoder();
#endif
    void sender_loop();

    std::shared_ptr<spdlog::logger>                            log_;
    std::shared_ptr<vulkan::display_provider>                  display_provider_;
    std::shared_ptr<switchboard>                               switchboard_;
    switchboard::network_writer<data_format::compressed_frame> frames_topic_;

    /**
     * @brief Cached head pose extracted from pose_with_hands
     *
     * Updated whenever new pose_with_hands data arrives.
     */
    mutable data_format::pose::fast_head_pose_type cached_head_pose_;

    /**
     * @brief Last processed pose timestamp to avoid duplicate hand tracking forwarding
     */
    mutable time_point last_processed_time_{};

    std::shared_ptr<vulkan::buffer_pool<BUFFER_TYPE>> buffer_pool_;

#ifdef OPENXR_CLIENT
    int framerate_ = 90;
#else
    int framerate_ = 144;
#endif
    long bitrate_ = OFFLOAD_RENDERING_BITRATE;

    bool use_pass_depth_          = false;
    bool use_pass_motion_vectors_ = false;
    bool nalu_only_               = false;

    std::atomic<bool> framebuffers_imported_{false};

    // Set after each color encode call and copied into compressed_frame::is_keyframe.
    bool color_frame_is_keyframe_ = false;

#ifdef NVENC_ENCODER
    // ========================================================================
    // NVENC-specific members
    // ========================================================================

    // Vulkan context for CUDA interop
    vulkan_context vk_ctx_;

    // NVENC encoders (one per eye for color, optionally for depth and motion vectors)
    std::array<std::unique_ptr<nvenc_encoder>, 2> color_encoder_;
    std::array<std::unique_ptr<nvenc_encoder>, 2> depth_encoder_;
    std::array<std::unique_ptr<nvenc_encoder>, 2> motion_vec_encoder_;

    // Imported image indices: [buffer_index][eye] for color, depth, and motion vectors
    std::vector<std::array<int, 2>> color_imported_indices_;
    std::vector<std::array<int, 2>> depth_imported_indices_;
    std::vector<std::array<int, 2>> motion_vec_imported_indices_;

#  ifdef COMBINED_ENCODING
    // Under COMBINED_ENCODING a single encoder handles both eyes at double width.
    // color_encoder_[0] is used; color_encoder_[1] is unused.
    // encode_out_combined_color_packet_ carries the single combined bitstream;
    // encode_out_color_packets_ is not used for color in this mode.
    PACKET_TYPE encode_out_combined_color_packet_{};
#  endif // COMBINED_ENCODING

#else
    // ========================================================================
    // FFmpeg-specific members
    // ========================================================================

    std::vector<std::array<vulkan::ffmpeg_utils::ffmpeg_vk_frame, 2>> avvk_color_frames_;
    std::vector<std::array<vulkan::ffmpeg_utils::ffmpeg_vk_frame, 2>> avvk_depth_frames_;

    AVBufferRef* device_ctx_      = nullptr;
    AVBufferRef* cuda_device_ctx_ = nullptr;
    AVBufferRef* frame_ctx_       = nullptr;
    AVBufferRef* cuda_frame_ctx_  = nullptr;

    AVCodecContext*         codec_color_ctx_ = nullptr;
    std::array<AVFrame*, 2> encode_src_color_frames_{};
#endif
    std::array<PACKET_TYPE, 2> encode_out_color_packets_{};
#ifndef NVENC_ENCODER
    AVCodecContext*         codec_depth_ctx_ = nullptr;
    std::array<AVFrame*, 2> encode_src_depth_frames_{};
#endif
    std::array<PACKET_TYPE, 2> encode_out_depth_packets_{};
    std::array<PACKET_TYPE, 2> encode_out_motion_vec_packets_{};

    uint64_t frame_count_ = 0;

    float near_z_{0.};
    float far_z_{0.};
    // Boxcar FPS: timestamps of frames encoded within the last 1 second.
    // Updated every frame in _p_one_iteration(); get_fps() returns the count.
    std::deque<std::chrono::high_resolution_clock::time_point> fps_window_;
    std::map<std::string, long long>                           metrics_;

    int32_t last_frame_ind_ = -1;
#ifdef OPENXR_CLIENT
    xrt_pose last_sent_pose_{};
#else
    data_format::pose::fast_head_pose_type last_sent_pose_{};
#endif

    struct illixr_framebuffer* framebuffer_array_ = nullptr;
    VkExtent2D                 extent_            = {0, 0};

    std::shared_ptr<pose_relay> pose_relay_;
    hmd_config                  hmd_setup_;

    std::atomic<bool> ready_{false};
    uint64_t          frame_number_{0};
    double            current_encode_time_{0.};

    std::deque<std::shared_ptr<data_format::compressed_frame>> send_queue_;

    std::mutex                                                send_queue_mutex_;
    std::condition_variable                                   send_queue_cv_;
    std::thread                                               sender_thread_;
    static constexpr size_t                                   MAX_QUEUE_DEPTH = 6;
    std::atomic<bool>                                         sender_running_{false};
    std::map<uint64_t, uint8_t>                               pose_usage_{};
    std::map<uint64_t, std::chrono::steady_clock::time_point> frame_timing_{};
#ifdef OPENXR_CLIENT
    float overscan_ = 1.f;
#endif
};
} // namespace ILLIXR
