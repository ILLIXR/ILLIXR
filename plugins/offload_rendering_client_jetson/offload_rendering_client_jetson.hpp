#pragma once

#include "decoding/video_decode.h"
#include "illixr/data_format/pose_prediction.hpp"
#include "illixr/data_format/serializable_data.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"
#include "illixr/vk/display_provider.hpp"
#include "illixr/vk/render_pass.hpp"

namespace ILLIXR {

/**
 * @class offload_rendering_client_jetson
 * @brief Main class implementing the offload rendering client functionality
 *
 * This class handles:
 * - Video decoding of received frames using hardware acceleration
 * - Pose synchronization with the server
 * - Integration with Vulkan display system
 * - DMA-buf based zero-copy frame transfer
 */
class offload_rendering_client_jetson
    : public threadloop
    , public vulkan::app
    , public std::enable_shared_from_this<offload_rendering_client_jetson> {
public:
    /**
     * @brief Constructor initializing the client components
     * @param name Plugin name
     * @param pb Phonebook for component lookup
     */
    offload_rendering_client_jetson(const std::string& name, phonebook* pb);

    // void start() override {
    //     threadloop::start();
    // }
    /**
     * @brief Initializes the video decoders
     * Sets up hardware-accelerated decoders for color and depth (if enabled) streams
     */
    void mmapi_init_decoders();

    /**
     * @brief Initializes Vulkan resources for frame handling
     * Sets up command pools, buffers and other Vulkan resources needed for frame processing
     */
    [[maybe_unused]] void vk_resources_init();

    /**
     * @brief Imports a DMA buffer into the NVIDIA buffer system
     * @param eye The Vulkan image representing the eye buffer to import
     */
    static void mmapi_import_dmabuf(vulkan::vk_image& eye);
    /**
     * @brief Setup function called by the display system
     * Initializes resources and prepares for frame processing
     */
    void setup(VkRenderPass render_pass, uint32_t subpass,
               std::shared_ptr<vulkan::buffer_pool<data_format::fast_pose_type>> buffer_pool) override;

    void record_command_buffer(VkCommandBuffer commandBuffer, VkFramebuffer framebuffer, int buffer_ind, bool left) override {
        (void) commandBuffer;
        (void) framebuffer;
        (void) buffer_ind;
        (void) left;
    }

    void update_uniforms(const data_format::pose_type& render_pose) override {
        (void) render_pose;
    }

    bool is_external() override {
        return true;
    }

    void destroy() override;
    void _p_thread_setup() override;

    // skip_option _p_should_skip() override {
    //     return threadloop::_p_should_skip();
    // }

    /**
     * @brief Handles image layout transitions in Vulkan
     * @param cmd_buf Command buffer to record the transition commands
     * @param image The image to transition
     * @param old_layout Original layout
     * @param new_layout Target layout
     */
    static void transition_layout(VkCommandBuffer cmd_buf, VkImage image, VkImageLayout old_layout, VkImageLayout new_layout);
    /**
     * @brief Copies decoded frame data to the display buffer
     * @param ind Buffer index
     * @param eye Eye index (0 for left, 1 for right)
     * @param fd File descriptor of the source buffer
     * @param depth Whether this is a depth buffer
     */
    [[maybe_unused]] void blitTo(uint8_t ind, uint8_t eye, int fd, bool depth);

    /**
     * @brief Queues a bytestream for decoding
     * Handles the decoding of received video frames and synchronization with poses
     */
    void queue_bytestream();

    /**
     * @brief Main processing loop iteration
     * Handles frame decoding, transfer and synchronization
     */
    void _p_one_iteration() override;

    /**
     * @brief Pushes the current pose to the server
     * Handles pose synchronization with the rendering server
     */
    void push_pose();
    /**
     * @brief Receives and processes network data
     * @return true if frame was received successfully, false otherwise
     */
    bool network_receive();
    void stop() override;

    std::shared_ptr<std::thread> decode_q_thread;

private:
    // Core components
    std::shared_ptr<switchboard>                                switchboard_;
    std::shared_ptr<spdlog::logger>                             log_;
    std::shared_ptr<vulkan::display_provider>                   display_provider_;
    switchboard::buffered_reader<data_format::compressed_frame> frames_reader_;
    switchboard::network_writer<data_format::fast_pose_type>    pose_writer_;
    std::shared_ptr<data_format::pose_prediction>               pose_prediction_;
    std::shared_ptr<relative_clock>                             clock_;

    // State flags
    std::atomic<bool> ready_{false};
    std::atomic<bool> running_{true};
    bool              use_depth_{false};
    // bool              resolutionRequeue{false};

    // Buffer management
    std::shared_ptr<vulkan::buffer_pool<data_format::fast_pose_type>> buffer_pool_;

    std::vector<std::array<VkCommandBuffer, 2>> blit_color_cb_;
    std::vector<std::array<VkCommandBuffer, 2>> blit_depth_cb_;
    std::vector<std::array<VkCommandBuffer, 2>> layout_transition_start_cmd_bufs_;
    std::vector<std::array<VkCommandBuffer, 2>> layout_transition_end_cmd_bufs_;

    // Decoders
    mmapi_decoder color_decoder_;
    mmapi_decoder depth_decoder_;

    // Frame and pose management
    std::shared_ptr<const data_format::compressed_frame> received_frame_;

    std::queue<data_format::fast_pose_type> pose_queue_;
    std::mutex                              pose_queue_mutex_;
    // pose_type                               fixed_pose_;

    // Vulkan resources
    VkCommandPool command_pool_{};
    VkFence       blit_fence_;

    // Performance metrics
    uint16_t                                       fps_counter_{0};
    std::chrono::high_resolution_clock::time_point fps_start_time_{std::chrono::high_resolution_clock::now()};
    std::map<std::string, uint32_t>                metrics_;

    // uint64_t                                       frame_count_{0};
    struct MemoryTypeResult {
        bool     found;
        uint32_t typeIndex;
    };

    static MemoryTypeResult findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter,
                                           VkMemoryPropertyFlags properties);
    [[maybe_unused]] void   submit_command_buffer(VkCommandBuffer vk_command_buffer);
};
} // namespace ILLIXR
