#pragma once

/**
 * @class offload_rendering_client
 * @brief Main client implementation for offload rendering
 *
 * This class handles:
 * 1. Reception of encoded frames from the server
 * 2. Hardware-accelerated HEVC decoding using FFmpeg/CUDA
 * 3. Color space conversion (NV12 to RGBA)
 * 4. Vulkan image management and synchronization
 * 5. Pose synchronization with the server
 *
 * The client supports two modes:
 * - Realtime mode: Receives and displays frames with real-time pose updates
 * - Comparison mode: Uses a fixed pose for image quality comparison
 *
 * Configuration is controlled through environment variables:
 * - ILLIXR_USE_DEPTH_IMAGES: Enable depth frame reception/decoding
 */
#define DOUBLE_INCLUDE
// ILLIXR core headers
#include "illixr/data_format/pose_prediction.hpp"
#include "illixr/data_format/serializable_data.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"
#undef DOUBLE_INCLUDE
// ILLIXR Vulkan headers
#include "illixr/vk/display_provider.hpp"
#include "illixr/vk/ffmpeg_utils.hpp"
#include "illixr/vk/render_pass.hpp"
#include "illixr/vk/vk_extension_request.hpp"
#include "illixr/vk/vulkan_utils.hpp"

// FFmpeg headers (C interface)
extern "C" {
#include "libavfilter_illixr/buffersink.h"
#include "libavfilter_illixr/buffersrc.h"
#include "libswscale_illixr/swscale.h"
}

// NVIDIA nppi headers
#include "nppi.h"

namespace ILLIXR {

class offload_rendering_client
    : public threadloop
    , public vulkan::app {
public:
    /**
     * @brief Constructor initializes the client with configuration from environment variables
     * @param name Plugin name
     * @param pb Phonebook for component lookup
     */
    offload_rendering_client(const std::string& name, phonebook* pb);

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
               std::shared_ptr<vulkan::buffer_pool<data_format::fast_pose_type>> buffer_pool) override;

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
    void update_uniforms(const data_format::pose_type& render_pose, bool left) override {
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

protected:
    /**
     * @brief Thread setup (no-op in this implementation)
     */
    void _p_thread_setup() override { }

    /**
     * @brief Determines if the current iteration should be skipped
     */
    skip_option _p_should_skip() override {
        return threadloop::_p_should_skip();
    }

    [[maybe_unused]] void copy_image_to_cpu_and_save_file(AVFrame* frame);
    [[maybe_unused]] void save_nv12_img_to_png(AVFrame* cuda_frame) const;
    void transition_layout(VkCommandBuffer cmd_buf, AVFrame* frame, VkImageLayout old_layout, VkImageLayout new_layout);

    /**
     * @brief Main processing loop for frame decoding and display
     *
     * This method:
     * 1. Sends the latest pose to the server
     * 2. Receives and decodes encoded frames
     * 3. Performs color space conversion
     * 4. Updates display buffers
     * 5. Tracks performance metrics
     */
    void _p_one_iteration() override;

private:
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

    std::shared_ptr<switchboard>                                switchboard_;
    std::shared_ptr<spdlog::logger>                             log_;
    std::shared_ptr<vulkan::display_provider>                   display_provider_;
    switchboard::buffered_reader<data_format::compressed_frame> frames_reader_;
    switchboard::network_writer<data_format::fast_pose_type>    pose_writer_;
    std::shared_ptr<data_format::pose_prediction>               pose_prediction_;
    std::atomic<bool>                                           ready_ = false;

    std::shared_ptr<vulkan::buffer_pool<data_format::fast_pose_type>> buffer_pool_;
    bool                                                              use_depth_ = false;
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

    data_format::fast_pose_type decoded_frame_pose_;

    VkCommandPool command_pool{};
    Npp8u*        yuv420_y_plane_ = nullptr;
    Npp8u*        yuv420_u_plane_ = nullptr;
    Npp8u*        yuv420_v_plane_ = nullptr;
    int           y_step_         = 0;
    int           u_step_         = 0;
    int           v_step_         = 0;

    uint64_t frame_count_ = 0;

    VkFence fence_{};

    uint16_t                                       fps_counter_    = 0;
    std::chrono::high_resolution_clock::time_point fps_start_time_ = std::chrono::high_resolution_clock::now();
    std::map<std::string, uint32_t>                metrics_{};

    std::shared_ptr<relative_clock> clock_;
};

} // namespace ILLIXR
