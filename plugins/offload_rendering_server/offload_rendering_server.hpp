#pragma once

#define DOUBLE_INCLUDE
#include "illixr/data_format/pose_prediction.hpp"
#include "illixr/data_format/serializable_data.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"
#include "illixr/vk/display_provider.hpp"
#include "illixr/vk/ffmpeg_utils.hpp"
#include "illixr/vk/render_pass.hpp"
#include "illixr/vk/vulkan_utils.hpp"
#undef DOUBLE_INCLUDE

namespace ILLIXR {

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
    void _p_thread_setup() override;

    /**
     * @brief Sets up the rendering pipeline and encoding resources
     * @param render_pass Vulkan render pass handle
     * @param subpass Subpass index
     * @param _buffer_pool Buffer pool for frame management
     * @param input_texture_vulkan_coordinates Whether input textures use Vulkan coordinates
     */
    void setup(VkRenderPass render_pass, uint32_t subpass,
               std::shared_ptr<vulkan::buffer_pool<data_format::fast_pose_type>> _buffer_pool,
               bool                                                              input_texture_vulkan_coordinates) override;

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
     */
    data_format::fast_pose_type get_fast_pose() const override;

    /**
     * @brief Get the true pose (same as fast pose in this implementation)
     */
    data_format::pose_type get_true_pose() const override {
        return get_fast_pose().pose;
    }

    /**
     * @brief Get predicted pose for a future time point (returns current pose)
     */
    data_format::fast_pose_type get_fast_pose(time_point future_time) const override {
        (void) future_time;
        return get_fast_pose();
    }

    /**
     * @brief Check if fast pose data is reliable
     */
    bool fast_pose_reliable() const override {
        return render_pose_.get_ro_nullable() != nullptr;
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
    data_format::pose_type correct_pose(const data_format::pose_type& pose) const override {
        (void) pose;
        return {};
    }

    /**
     * @brief Record command buffer (no-op in this implementation)
     */
    void record_command_buffer(VkCommandBuffer commandBuffer, VkFramebuffer framebuffer, int buffer_ind, bool left, VkFence fence) override {
        (void) commandBuffer;
        (void) framebuffer;
        (void) buffer_ind;
        (void) left;
        (void) fence;
    }

    /**
     * @brief Update uniforms (no-op in this implementation)
     */
    void update_uniforms(const data_format::fast_pose_type& render_pose, bool left) override {
        (void) render_pose;
        (void) left;
    }

    void save_frame(VkFence fence) override {
        (void) fence;
    }

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
    void _p_one_iteration() override;

private:
    /**
     * @brief Sends encoded frame data to the client
     * @param pose The pose data associated with the frame
     */
    void enqueue_for_network_send(data_format::fast_pose_type& pose);

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

    std::shared_ptr<spdlog::logger>                                   log_;
    std::shared_ptr<vulkan::display_provider>                         display_provider_;
    std::shared_ptr<switchboard>                                      switchboard_;
    switchboard::network_writer<data_format::compressed_frame>        frames_topic_;
    switchboard::reader<data_format::fast_pose_type>                  render_pose_;
    std::shared_ptr<vulkan::buffer_pool<data_format::fast_pose_type>> buffer_pool_;
    std::vector<std::array<vulkan::ffmpeg_utils::ffmpeg_vk_frame, 2>> avvk_color_frames_;
    std::vector<std::array<vulkan::ffmpeg_utils::ffmpeg_vk_frame, 2>> avvk_depth_frames_;

    int  framerate_     = 144;
    long color_bitrate_ = OFFLOAD_RENDERING_COLOR_BITRATE;
    long depth_bitrate_ = OFFLOAD_RENDERING_DEPTH_BITRATE;

    bool use_pass_depth_ = false;
    bool nalu_only_      = false;

    AVBufferRef* device_ctx_      = nullptr;
    AVBufferRef* cuda_device_ctx_ = nullptr;
    AVBufferRef* frame_ctx_       = nullptr;
    AVBufferRef* cuda_frame_ctx_  = nullptr;

    AVCodecContext*          codec_color_ctx_ = nullptr;
    std::array<AVFrame*, 2>  encode_src_color_frames_{};
    std::array<AVPacket*, 2> encode_out_color_packets_{};

    AVCodecContext*          codec_depth_ctx_ = nullptr;
    std::array<AVFrame*, 2>  encode_src_depth_frames_{};
    std::array<AVPacket*, 2> encode_out_depth_packets_{};

    uint64_t frame_count_ = 0;

    double                                         fps_counter_    = 0;
    std::chrono::high_resolution_clock::time_point fps_start_time_ = std::chrono::high_resolution_clock::now();
    std::map<std::string, uint32_t>                metrics_;

    uint16_t last_frame_ind_ = -1;

    std::atomic<bool> ready_{false};
};
} // namespace ILLIXR
