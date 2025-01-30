/**
 * @file plugin.cpp
 * @brief ILLIXR Offload Rendering Client Plugin for Jetson
 *
 * This plugin implements the client-side functionality for offloaded rendering on Jetson devices.
 * It handles video decoding, pose synchronization, and Vulkan-based display integration.
 */

#include "decoding/video_decode.h"
#include "illixr/data_format.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/pose_prediction.hpp"
#include "illixr/serializable_data.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"
#include "illixr/vk/display_provider.hpp"
#include "illixr/vk/render_pass.hpp"
#include "illixr/vk/vk_extension_request.h"
#include "illixr/vk/vulkan_utils.hpp"

#include <bitset>
#include <cstdlib>
#include <set>

#define OFFLOAD_RENDERING_FFMPEG_DECODER_NAME "h264"

using namespace ILLIXR;

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
    offload_rendering_client_jetson(const std::string& name, phonebook* pb)
        : threadloop{name, pb}
        , sb{pb->lookup_impl<switchboard>()}
        , log{spdlogger("debug")}
        , dp{pb->lookup_impl<vulkan::display_provider>()}
        , frames_reader{sb->get_buffered_reader<compressed_frame>("compressed_frames")}
        , pose_writer{sb->get_network_writer<fast_pose_type>("render_pose", {})}
        , pp{pb->lookup_impl<pose_prediction>()}
        , clock{pb->lookup_impl<RelativeClock>()} {
        // Check environment variables for configuration
        use_depth = std::getenv("ILLIXR_USE_DEPTH_IMAGES") != nullptr && std::stoi(std::getenv("ILLIXR_USE_DEPTH_IMAGES"));
        if (use_depth) {
            log->debug("Encoding depth images for the client");
        } else {
            log->debug("Not encoding depth images for the client");
        }

        compare_images = std::getenv("ILLIXR_COMPARE_IMAGES") != nullptr && std::stoi(std::getenv("ILLIXR_COMPARE_IMAGES"));
        if (compare_images) {
            log->debug("Sending constant pose to compare images");
            // Fixed pose for image comparison testing
            fixed_pose = pose_type(time_point(), Eigen::Vector3f(-0.912881, 0.729179, -0.527451),
                                   Eigen::Quaternionf(0.994709, 0.0463598, -0.0647321, 0.0649133));
        } else {
            log->debug("Client sending normal poses (not comparing images)");
        }
    }

    void start() override {
        threadloop::start();
    }

    /**
     * @brief Initializes the video decoders
     * Sets up hardware-accelerated decoders for color and depth (if enabled) streams
     */
    void mmapi_init_decoders() {
        auto ret = color_decoder.decoder_init();
        assert(ret == 0);
        if (use_depth) {
            ret = depth_decoder.decoder_init();
            assert(ret == 0);
        }
    }

    /**
     * @brief Initializes Vulkan resources for frame handling
     * Sets up command pools, buffers and other Vulkan resources needed for frame processing
     */
    void vk_resources_init() {
        command_pool = vulkan::create_command_pool(dp->vk_device, dp->queues[vulkan::queue::GRAPHICS].family);

        blit_color_cb.resize(buffer_pool->image_pool.size());
        blit_depth_cb.resize(buffer_pool->image_pool.size());

        // Initialize command buffers for each eye and frame
        for (size_t i = 0; i < buffer_pool->image_pool.size(); i++) {
            for (auto eye = 0; eye < 2; eye++) {
                VkCommandBufferAllocateInfo allocInfo{};
                allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
                allocInfo.commandPool        = command_pool;
                allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                allocInfo.commandBufferCount = 1;

                VK_ASSERT_SUCCESS(vkAllocateCommandBuffers(dp->vk_device, &allocInfo, &blit_color_cb[i][eye]));
                if (use_depth) {
                    VK_ASSERT_SUCCESS(vkAllocateCommandBuffers(dp->vk_device, &allocInfo, &blit_depth_cb[i][eye]));
                }
            }
        }
    }

    /**
     * @brief Imports a DMA buffer into the NVIDIA buffer system
     * @param eye The Vulkan image representing the eye buffer to import
     */
    void mmapi_import_dmabuf(vulkan::vk_image& eye) {
        // Setup NVIDIA buffer surface parameters
        NvBufSurfaceMapParams params{};
        params.num_planes                  = 1;
        params.gpuId                       = 0;
        params.fd                          = eye.fd;
        params.totalSize                   = eye.allocation_info.size;
        params.memType                     = NVBUF_MEM_SURFACE_ARRAY;
        params.layout                      = NVBUF_LAYOUT_BLOCK_LINEAR;
        params.scanformat                  = NVBUF_DISPLAYSCANFORMAT_PROGRESSIVE;
        params.colorFormat                 = NVBUF_COLOR_FORMAT_BGRA;
        params.planes[0].width             = eye.image_info.extent.width;
        params.planes[0].height            = eye.image_info.extent.height;
        params.planes[0].pitch             = eye.image_info.extent.width * 4;
        params.planes[0].offset            = 0;
        params.planes[0].psize             = eye.allocation_info.size;
        params.planes[0].secondfieldoffset = 0;
        params.planes[0].blockheightlog2   = 4;

        NvBufSurface* surface;
        auto          ret  = NvBufSurfaceImport(&surface, &params);
        surface->numFilled = 1;
        assert(ret == 0);
    }

    /**
     * @brief Setup function called by the display system
     * Initializes resources and prepares for frame processing
     */
    virtual void setup(VkRenderPass render_pass, uint32_t subpass,
                       std::shared_ptr<vulkan::buffer_pool<fast_pose_type>> buffer_pool) override {
        this->buffer_pool = buffer_pool;
        vk_resources_init();

        // Import DMA buffers for all images
        for (auto& image : buffer_pool->image_pool) {
            for (auto& eye : image) {
                mmapi_import_dmabuf(eye);
            }
        }

        if (use_depth) {
            for (auto& image : buffer_pool->depth_image_pool) {
                for (auto& eye : image) {
                    mmapi_import_dmabuf(eye);
                }
            }
        }

        ready = true;

        // Initialize image layouts
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

        layout_transition_start_cmd_bufs.resize(buffer_pool->image_pool.size());
        layout_transition_end_cmd_bufs.resize(buffer_pool->image_pool.size());

        for (size_t i = 0; i < buffer_pool->image_pool.size(); i++) {
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

        VkFenceCreateInfo blitFence_info = {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        };

        vkCreateFence(dp->vk_device, &blitFence_info, nullptr, &blitFence);
    }

    void record_command_buffer(VkCommandBuffer commandBuffer, VkFramebuffer framebuffer, int buffer_ind, bool left) override { }

    void update_uniforms(const pose_type& render_pose) override { }

    bool is_external() override {
        return true;
    }

    void destroy() override {
        color_decoder.decoder_destroy();
        if (use_depth) {
            depth_decoder.decoder_destroy();
        }
    }

    std::shared_ptr<std::thread> decode_q_thread;

    void _p_thread_setup() override {
        mmapi_init_decoders();
        // open file "left.h264" for writing

        decode_q_thread = std::make_shared<std::thread>([&]() {
            // auto file = fopen("left.h264", "wb");
            auto frame = received_frame;
            while (running.load()) {
                queue_bytestream();
            }
        });
    }

    skip_option _p_should_skip() override {
        return threadloop::_p_should_skip();
    }

    /**
     * @brief Handles image layout transitions in Vulkan
     * @param cmd_buf Command buffer to record the transition commands
     * @param image The image to transition
     * @param old_layout Original layout
     * @param new_layout Target layout
     */
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

        // Configure barrier based on layout transition type
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
        } else if (old_layout == VK_IMAGE_LAYOUT_GENERAL && new_layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
            barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            src_stage             = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            dst_stage             = VK_PIPELINE_STAGE_TRANSFER_BIT;
        } else if (old_layout == VK_IMAGE_LAYOUT_GENERAL && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            src_stage             = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            dst_stage             = VK_PIPELINE_STAGE_TRANSFER_BIT;
        } else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_GENERAL) {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
            src_stage             = VK_PIPELINE_STAGE_TRANSFER_BIT;
            dst_stage             = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        } else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_GENERAL) {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
            src_stage             = VK_PIPELINE_STAGE_TRANSFER_BIT;
            dst_stage             = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        } else if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            src_stage             = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            dst_stage             = VK_PIPELINE_STAGE_TRANSFER_BIT;
        } else {
            throw std::invalid_argument("unsupported layout transition");
        }

        vkCmdPipelineBarrier(cmd_buf, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    std::array<VkImage, 2> importedImages      = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    std::array<VkImage, 2> importedDepthImages = {VK_NULL_HANDLE, VK_NULL_HANDLE};

    /**
     * @brief Copies decoded frame data to the display buffer
     * @param ind Buffer index
     * @param eye Eye index (0 for left, 1 for right)
     * @param fd File descriptor of the source buffer
     * @param depth Whether this is a depth buffer
     */
    void blitTo(uint8_t ind, uint8_t eye, int fd, bool depth) {
        VkImage  vkImage = VK_NULL_HANDLE;
        uint32_t width   = buffer_pool->image_pool[ind][eye].image_info.extent.width;
        uint32_t height  = buffer_pool->image_pool[ind][eye].image_info.extent.height;
        auto     blitCB  = depth ? blit_depth_cb[ind][eye] : blit_color_cb[ind][eye];

        // Create and set up the Vulkan image for the decoded frame
        if (vkImage == VK_NULL_HANDLE) {
            VkExternalMemoryImageCreateInfo dmaBufExternalMemoryImageCreateInfo{};
            dmaBufExternalMemoryImageCreateInfo.sType       = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
            dmaBufExternalMemoryImageCreateInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

            VkImageCreateInfo dmaBufImageCreateInfo{};
            dmaBufImageCreateInfo.sType       = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            dmaBufImageCreateInfo.pNext       = &dmaBufExternalMemoryImageCreateInfo;
            dmaBufImageCreateInfo.imageType   = VK_IMAGE_TYPE_2D;
            dmaBufImageCreateInfo.format      = buffer_pool->image_pool[ind][eye].image_info.format;
            dmaBufImageCreateInfo.extent      = {width, height, 1};
            dmaBufImageCreateInfo.mipLevels   = 1;
            dmaBufImageCreateInfo.arrayLayers = 1;
            dmaBufImageCreateInfo.samples     = VK_SAMPLE_COUNT_1_BIT;
            dmaBufImageCreateInfo.tiling      = buffer_pool->image_pool[ind][eye].image_info.tiling;
            dmaBufImageCreateInfo.usage       = VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
            dmaBufImageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            VK_ASSERT_SUCCESS(vkCreateImage(dp->vk_device, &dmaBufImageCreateInfo, nullptr, &vkImage));

            // Import the DMA-buf memory
            const int            duppedFd = dup(fd);
            VkMemoryRequirements imageMemoryRequirements{};
            vkGetImageMemoryRequirements(dp->vk_device, vkImage, &imageMemoryRequirements);

            const MemoryTypeResult memoryTypeResult = findMemoryType(
                dp->vk_physical_device, imageMemoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

            VkMemoryDedicatedAllocateInfo dedicatedAllocateInfo{};
            dedicatedAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
            dedicatedAllocateInfo.image = vkImage;

            VkImportMemoryFdInfoKHR importFdInfo{};
            importFdInfo.sType      = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR;
            importFdInfo.pNext      = &dedicatedAllocateInfo;
            importFdInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
            importFdInfo.fd         = duppedFd;

            VkMemoryAllocateInfo memoryAllocateInfo{};
            memoryAllocateInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            memoryAllocateInfo.pNext           = &importFdInfo;
            memoryAllocateInfo.allocationSize  = imageMemoryRequirements.size;
            memoryAllocateInfo.memoryTypeIndex = memoryTypeResult.typeIndex;

            VkDeviceMemory importedImageMemory;
            VK_ASSERT_SUCCESS(vkAllocateMemory(dp->vk_device, &memoryAllocateInfo, nullptr, &importedImageMemory));
            VK_ASSERT_SUCCESS(vkBindImageMemory(dp->vk_device, vkImage, importedImageMemory, 0));
        }

        // Record and submit the copy commands
        VK_ASSERT_SUCCESS(vkResetCommandBuffer(blitCB, 0));
        VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        VK_ASSERT_SUCCESS(vkBeginCommandBuffer(blitCB, &beginInfo));

        transition_layout(blitCB, vkImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

        VkImageCopy region{};
        region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.srcSubresource.layerCount = 1;
        region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.dstSubresource.layerCount = 1;
        region.extent.width              = width;
        region.extent.height             = height;
        region.extent.depth              = 1;

        vkCmdCopyImage(blitCB, vkImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       depth ? buffer_pool->depth_image_pool[ind][eye].image : buffer_pool->image_pool[ind][eye].image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        vkEndCommandBuffer(blitCB);

        // Submit all command buffers in sequence
        std::vector<VkCommandBuffer> cmd_bufs = {layout_transition_start_cmd_bufs[ind][eye], blitCB,
                                                 layout_transition_end_cmd_bufs[ind][eye]};

        VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submitInfo.commandBufferCount = static_cast<uint32_t>(cmd_bufs.size());
        submitInfo.pCommandBuffers    = cmd_bufs.data();

        vulkan::locked_queue_submit(dp->queues[vulkan::queue::GRAPHICS], 1, &submitInfo, blitFence);
        VK_ASSERT_SUCCESS(vkWaitForFences(dp->vk_device, 1, &blitFence, VK_TRUE, UINT64_MAX));
    }

    /**
     * @brief Queues a bytestream for decoding
     * Handles the decoding of received video frames and synchronization with poses
     */
    void queue_bytestream() {
        push_pose();
        if (!network_receive()) {
            return;
        }
        auto frame = received_frame;

        // Queue color and depth frames for decoding
        color_decoder.queue_output_plane_buffer(frame->left_color_nalu, frame->left_color_nalu_size);
        color_decoder.queue_output_plane_buffer(frame->right_color_nalu, frame->right_color_nalu_size);

        if (use_depth) {
            depth_decoder.queue_output_plane_buffer(frame->left_depth_nalu, frame->left_depth_nalu_size);
            depth_decoder.queue_output_plane_buffer(frame->right_depth_nalu, frame->right_depth_nalu_size);
        }
    }

    /**
     * @brief Main processing loop iteration
     * Handles frame decoding, transfer and synchronization
     */
    void _p_one_iteration() override {
        if (!ready) {
            return;
        }

        auto ind        = buffer_pool->src_acquire_image();
        auto decode_end = std::chrono::high_resolution_clock::now();

        // Decode color and depth frames in parallel
        std::thread color = std::thread([&] {
            for (auto eye = 0; eye < 2; eye++) {
                auto ret = color_decoder.dec_capture(buffer_pool->image_pool[ind][eye].fd);
                while (ret == -EAGAIN) {
                    ret = color_decoder.dec_capture(buffer_pool->image_pool[ind][eye].fd);
                }
                assert(ret == 0 || ret == -EAGAIN);
            }
        });

        if (use_depth) {
            std::thread depth = std::thread([&] {
                for (auto eye = 0; eye < 2; eye++) {
                    auto ret = depth_decoder.dec_capture(buffer_pool->depth_image_pool[ind][eye].fd);
                    while (ret == -EAGAIN) {
                        ret = depth_decoder.dec_capture(buffer_pool->depth_image_pool[ind][eye].fd);
                    }
                    assert(ret == 0 || ret == -EAGAIN);
                }
            });
            depth.join();
        }
        color.join();

        auto           transfer_end = std::chrono::high_resolution_clock::now();
        fast_pose_type decoded_frame_pose;
        {
            std::lock_guard<std::mutex> lock(pose_queue_mutex);
            decoded_frame_pose = pose_queue.front();
            pose_queue.pop();
        }
        buffer_pool->src_release_image(ind, std::move(decoded_frame_pose));

        // Calculate and track latency metrics
        auto now =
            time_point{std::chrono::duration<long, std::nano>{std::chrono::high_resolution_clock::now().time_since_epoch()}};
        auto pipeline_latency = now - decoded_frame_pose.predict_target_time;

        metrics["capture"] += std::chrono::duration_cast<std::chrono::microseconds>(transfer_end - decode_end).count();
        metrics["pipeline"] += std::chrono::duration_cast<std::chrono::microseconds>(pipeline_latency).count();

        // Update FPS and metrics every second
        if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - fps_start_time)
                .count() >= 1) {
            log->info("Decoder FPS: {}", fps_counter);
            fps_start_time = std::chrono::high_resolution_clock::now();

            for (auto& metric : metrics) {
                auto fps = std::max(fps_counter, (uint16_t) 0);
                log->info("{}: {}", metric.first, metric.second / (double) (fps));
                metric.second = 0;
            }
            fps_counter = 0;
        } else {
            fps_counter++;
        }
    }

    /**
     * @brief Pushes the current pose to the server
     * Handles pose synchronization with the rendering server
     */
    void push_pose() {
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

    /**
     * @brief Receives and processes network data
     * @return true if frame was received successfully, false otherwise
     */
    bool network_receive() {
        received_frame = frames_reader.dequeue();
        if (received_frame == nullptr) {
            return false;
        }

        uint64_t timestamp =
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch())
                .count();
        auto pose            = received_frame->pose;
        long network_latency = (long) timestamp - (long) received_frame->sent_time;
        metrics["network"] += network_latency / 1000000;
        pose.predict_target_time = time_point{std::chrono::duration<long, std::nano>{timestamp}};
        {
            std::lock_guard<std::mutex> lock(pose_queue_mutex);
            pose_queue.push(pose);
        }
        return true;
    }

    void stop() override {
        exit(0);
        running = false;
        decode_q_thread->join();
        threadloop::stop();
    }

private:
    // Core components
    std::shared_ptr<switchboard>                   sb;
    std::shared_ptr<spdlog::logger>                log;
    std::shared_ptr<vulkan::display_provider>      dp;
    switchboard::buffered_reader<compressed_frame> frames_reader;
    switchboard::network_writer<fast_pose_type>    pose_writer;
    std::shared_ptr<pose_prediction>               pp;
    std::shared_ptr<RelativeClock>                 clock;

    // State flags
    std::atomic<bool> ready{false};
    std::atomic<bool> running{true};
    bool              use_depth{false};
    bool              compare_images{false};
    bool              resolutionRequeue{false};

    // Buffer management
    std::shared_ptr<vulkan::buffer_pool<fast_pose_type>> buffer_pool;
    std::vector<std::array<VkCommandBuffer, 2>>          blit_color_cb;
    std::vector<std::array<VkCommandBuffer, 2>>          blit_depth_cb;
    std::vector<std::array<VkCommandBuffer, 2>>          layout_transition_start_cmd_bufs;
    std::vector<std::array<VkCommandBuffer, 2>>          layout_transition_end_cmd_bufs;

    // Decoders
    mmapi_decoder color_decoder;
    mmapi_decoder depth_decoder;

    // Frame and pose management
    std::shared_ptr<const compressed_frame> received_frame;
    std::queue<fast_pose_type>              pose_queue;
    std::mutex                              pose_queue_mutex;
    pose_type                               fixed_pose;

    // Vulkan resources
    VkCommandPool          command_pool{};
    VkFence                blitFence;
    std::array<VkImage, 2> importedImages{VK_NULL_HANDLE, VK_NULL_HANDLE};
    std::array<VkImage, 2> importedDepthImages{VK_NULL_HANDLE, VK_NULL_HANDLE};

    // Performance metrics
    uint16_t                                       fps_counter{0};
    std::chrono::high_resolution_clock::time_point fps_start_time{std::chrono::high_resolution_clock::now()};
    std::map<std::string, uint32_t>                metrics;
    uint64_t                                       frame_count{0};

    struct MemoryTypeResult {
        bool     found;
        uint32_t typeIndex;
    };

    MemoryTypeResult findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
        VkPhysicalDeviceMemoryProperties memoryProperties;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);

        MemoryTypeResult result;
        result.found = false;

        for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i) {
            if ((typeFilter & (1 << i)) && (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                result.typeIndex = i;
                result.found     = true;
                break;
            }
        }
        return result;
    }

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
};

/**
 * @class offload_rendering_client_jetson_loader
 * @brief Plugin loader class for the offload rendering client
 *
 * Handles plugin registration and Vulkan extension setup
 */
class offload_rendering_client_jetson_loader
    : public plugin
    , public vulkan::vk_extension_request {
public:
    offload_rendering_client_jetson_loader(const std::string& name, phonebook* pb)
        : plugin(name, pb)
        , offload_rendering_client_jetson_plugin{std::make_shared<offload_rendering_client_jetson>(name, pb)} {
        pb->register_impl<vulkan::app>(offload_rendering_client_jetson_plugin);
    }

    std::vector<const char*> get_required_instance_extensions() override {
        return {VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME, VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME,
                VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME};
    }

    std::vector<const char*> get_required_devices_extensions() override {
        return {VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME, VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
                VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME, VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
                VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME};
    }

    void start() override {
        offload_rendering_client_jetson_plugin->start();
    }

    void stop() override {
        offload_rendering_client_jetson_plugin->stop();
    }

private:
    std::shared_ptr<offload_rendering_client_jetson> offload_rendering_client_jetson_plugin;
};

PLUGIN_MAIN(offload_rendering_client_jetson_loader)