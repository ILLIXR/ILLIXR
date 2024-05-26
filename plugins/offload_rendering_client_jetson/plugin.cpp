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

#include <cstdlib>
#include <set>

#define OFFLOAD_RENDERING_FFMPEG_DECODER_NAME "h264"

using namespace ILLIXR;

class offload_rendering_client
    : public threadloop
    , public vulkan::app
    , public std::enable_shared_from_this<offload_rendering_client> {
public:
    offload_rendering_client(const std::string& name, phonebook* pb)
        : threadloop{name, pb}
        , sb{pb->lookup_impl<switchboard>()}
        , log{spdlogger("debug")}
        , dp{pb->lookup_impl<vulkan::display_provider>()}
        , frames_reader{sb->get_buffered_reader<compressed_frame>("compressed_frames")}
        , pose_writer{sb->get_network_writer<fast_pose_type>("render_pose", {})}
        , pp{pb->lookup_impl<pose_prediction>()}
        , clock{pb->lookup_impl<RelativeClock>()} {
        use_depth = std::getenv("ILLIXR_USE_DEPTH_IMAGES") != nullptr && std::stoi(std::getenv("ILLIXR_USE_DEPTH_IMAGES"));
        if (use_depth) {
            log->debug("Encoding depth images for the client");
        } else {
            log->debug("Not encoding depth images for the client");
        }

        compare_images = std::getenv("ILLIXR_COMPARE_IMAGES") != nullptr && std::stoi(std::getenv("ILLIXR_COMPARE_IMAGES"));
        if (compare_images) {
            log->debug("Sending constant pose to compare images");
            // Constant pose as recorded from the GT. Note that the Quaternion constructor takes the w component first.

            // 0 ms
            // Timepoint: 25205 ms; Pose Position: -0.891115 0.732361 -0.536178; Pose Orientiation: 0.0519684 -0.113465
            // 0.0679164 0.989855
            // fixed_pose = pose_type(time_point(), Eigen::Vector3f(-0.891115, 0.732361, -0.536178),
            //                        Eigen::Quaternionf(0.989855, 0.0519684, -0.113465, 0.0679164));

            // -25 ms
            // Timepoint: 25177 ms; Pose Position: -0.897286 0.732254 -0.533516; Pose Orientiation: 0.050736 -0.112192 0.0657815
            // 0.990208
            // fixed_pose = pose_type(time_point(), Eigen::Vector3f(-0.897286, 0.732254, -0.533516),
            //                        Eigen::Quaternionf(0.990208, 0.050736, -0.112192, 0.0657815));

            // -50 ms
            // Timepoint: 25156 ms; Pose Position: -0.901611 0.732027 -0.531002; Pose Orientiation: 0.0499585 -0.110069 0.064728
            // 0.990555
            // fixed_pose = pose_type(time_point(), Eigen::Vector3f(-0.901611, 0.732027, -0.531002),
            //                        Eigen::Quaternionf(0.990555, 0.0499585, -0.110069, 0.064728));

            // -75 ms
            // Timepoint: 25128 ms; Pose Position: -0.90597 0.731928 -0.527296; Pose Orientiation: 0.0492738 -0.105977 0.0637901
            // 0.991096
            // fixed_pose = pose_type(time_point(), Eigen::Vector3f(-0.90597, 0.731928, -0.527296),
            //                        Eigen::Quaternionf(0.991096, 0.0492738, -0.105977, 0.0637901));

            // -100 ms
            // Timepoint: 25103 ms; Pose Position: -0.906131 0.732548 -0.526771; Pose Orientiation: 0.0486073 -0.10057 0.0633693
            // 0.991719
            // fixed_pose = pose_type(time_point(), Eigen::Vector3f(-0.906131, 0.732548, -0.526771),
            //                        Eigen::Quaternionf(0.991719, 0.0486073, -0.10057, 0.0633693));

            // -125 ms
            // Timepoint: 25080 ms; Pose Position: -0.906863 0.732095 -0.527118; Pose Orientiation: 0.0478971 -0.0949276
            // 0.0636839 0.99229
            // fixed_pose = pose_type(time_point(), Eigen::Vector3f(-0.906863, 0.732095, -0.527118),
            //                        Eigen::Quaternionf(0.99229, 0.0478971, -0.0949276, 0.0636839));

            // -150 ms
            // Timepoint: 25052 ms; Pose Position: -0.907584 0.731722 -0.527914; Pose Orientiation: 0.0473371 -0.0867409
            // 0.0639834 0.993047
            // fixed_pose = pose_type(time_point(), Eigen::Vector3f(-0.907584, 0.731722, -0.527914),
            //                        Eigen::Quaternionf(0.993047, 0.0473371, -0.0867409, 0.0639834));

            // -175 ms
            // Timepoint: 25032 ms; Pose Position: -0.909664 0.73097 -0.527997; Pose Orientiation: 0.0470376 -0.0787137
            // 0.0641232 0.99372
            // fixed_pose = pose_type(time_point(), Eigen::Vector3f(-0.909664, 0.73097, -0.527997),
            //                        Eigen::Quaternionf(0.99372, 0.0470376, -0.0787137, 0.0641232));

            // -200 ms
            // Timepoint: 25003 ms; Pose Position: -0.912881 0.729179 -0.527451; Pose Orientiation: 0.0463598 -0.0647321
            // 0.0649133 0.994709
            fixed_pose = pose_type(time_point(), Eigen::Vector3f(-0.912881, 0.729179, -0.527451),
                                   Eigen::Quaternionf(0.994709, 0.0463598, -0.0647321, 0.0649133));
        } else {
            log->debug("Cliont sending normal poses (not comparing images)");
        }
    }

    void start() override {
        threadloop::start();
    }

    void mmapi_init_decoders() {
        for (auto eye = 0; eye < 2; eye++) {
            auto ret = color_decoders[eye].decoder_init();
            assert(ret == 0);
            if (use_depth) {
                ret = depth_decoders[eye].decoder_init();
                assert(ret == 0);
            }
        }
    }

    void vk_resources_init() {
        command_pool = vulkan::create_command_pool(dp->vk_device, dp->queues[vulkan::queue::GRAPHICS].family);

        blit_color_cb.resize(buffer_pool->image_pool.size());
        blit_depth_cb.resize(buffer_pool->image_pool.size());

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

    virtual void setup(VkRenderPass render_pass, uint32_t subpass,
                       std::shared_ptr<vulkan::buffer_pool<fast_pose_type>> buffer_pool) override {
        this->buffer_pool = buffer_pool;
        vk_resources_init();

        ready = true;

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
        for (auto eye = 0; eye < 2; eye++) {
            color_decoders[eye].decoder_destroy();
            if (use_depth) {
                depth_decoders[eye].decoder_destroy();
            }
        }
    }

    std::shared_ptr<std::thread> decode_q_thread;

    void _p_thread_setup() override {
        mmapi_init_decoders();
        // open file "left.h264" for writing

        decode_q_thread = std::make_shared<std::thread>([&]() {
            // auto file = fopen("left.h264", "wb");
            auto frame = received_frame;
            while (running) {
                push_pose();
                if (!network_receive()) {
                    return;
                }
                frame = received_frame;

                // system timestamp
//                auto timestamp = std::chrono::high_resolution_clock::now();
//                auto diff      = timestamp - decoded_frame_pose.predict_target_time._m_time_since_epoch;
                // log->info("diff (ms): {}", diff.time_since_epoch().count() / 1000000.0);

                // log->debug("Position: {}, {}, {}", pose.position[0], pose.position[1], pose.position[2]);

                auto decode_start = std::chrono::high_resolution_clock::now();
                // receive packets
                color_decoders[0].queue_output_plane_buffer(frame->left_color_nalu, frame->left_color_nalu_size);
                color_decoders[1].queue_output_plane_buffer(frame->right_color_nalu,
                                                            frame->right_color_nalu_size);

                // write to file
                // fwrite(received_frame->left_color_nalu, 1, received_frame->left_color_nalu_size, file);
                // fflush(file);

                if (use_depth) {
                    depth_decoders[0].queue_output_plane_buffer(frame->left_depth_nalu, frame->left_depth_nalu_size);
                    depth_decoders[1].queue_output_plane_buffer(frame->right_depth_nalu, frame->right_depth_nalu_size);
                }
            }
        });
    }

    skip_option _p_should_skip() override {
        return threadloop::_p_should_skip();
    }

    // Vulkan layout transition
    // supports: shader read <-> transfer dst
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
        } else {
            throw std::invalid_argument("unsupported layout transition");
        }

        vkCmdPipelineBarrier(cmd_buf, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    std::array<VkImage, 2> importedImages = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    std::array<VkImage, 2> importedDepthImages = {VK_NULL_HANDLE, VK_NULL_HANDLE};

    void blitTo(uint8_t ind, uint8_t eye, int fd, bool depth) {
        VkImage* vkImage = depth ? &importedDepthImages[eye] : &importedImages[eye];
        uint32_t width  = buffer_pool->image_pool[ind][eye].image_info.extent.width;
        uint32_t height = buffer_pool->image_pool[ind][eye].image_info.extent.height;
        auto blitCB     = depth ? blit_depth_cb[ind][eye] : blit_color_cb[ind][eye];
        if (*vkImage == nullptr) {
            { // create vk image
                VkExternalMemoryImageCreateInfo dmaBufExternalMemoryImageCreateInfo{};
                dmaBufExternalMemoryImageCreateInfo.sType       = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
                dmaBufExternalMemoryImageCreateInfo.pNext       = nullptr;
                dmaBufExternalMemoryImageCreateInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

                VkImageCreateInfo dmaBufImageCreateInfo{};
                dmaBufImageCreateInfo.sType                 = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
                dmaBufImageCreateInfo.pNext                 = &dmaBufExternalMemoryImageCreateInfo;
                dmaBufImageCreateInfo.flags                 = 0;
                dmaBufImageCreateInfo.imageType             = VK_IMAGE_TYPE_2D;
                dmaBufImageCreateInfo.format                = buffer_pool->image_pool[ind][eye].image_info.format;
                dmaBufImageCreateInfo.extent                = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
                dmaBufImageCreateInfo.mipLevels             = 1;
                dmaBufImageCreateInfo.arrayLayers           = 1;
                dmaBufImageCreateInfo.samples               = VK_SAMPLE_COUNT_1_BIT;
                dmaBufImageCreateInfo.tiling                = buffer_pool->image_pool[ind][eye].image_info.tiling;
                dmaBufImageCreateInfo.usage                 = VK_IMAGE_LAYOUT_GENERAL;
                dmaBufImageCreateInfo.sharingMode           = VK_SHARING_MODE_EXCLUSIVE;
                dmaBufImageCreateInfo.queueFamilyIndexCount = 0;
                dmaBufImageCreateInfo.pQueueFamilyIndices   = nullptr;
                dmaBufImageCreateInfo.initialLayout         = VK_IMAGE_LAYOUT_GENERAL;
                VK_ASSERT_SUCCESS(vkCreateImage(dp->vk_device, &dmaBufImageCreateInfo, nullptr, vkImage));
            }

            VkDeviceMemory importedImageMemory;

            { // allocate and bind
                const int duppedFd = dup(fd);
                (void) (duppedFd);
                //            auto duppedFd = fd;

                log->info("FD {} dupped to {}, eye {}, depth {}", fd, duppedFd, eye, depth);

                auto vkGetMemoryFdPropertiesKHR =
                    (PFN_vkGetMemoryFdPropertiesKHR) vkGetInstanceProcAddr(dp->vk_instance, "vkGetMemoryFdPropertiesKHR");
                assert(vkGetMemoryFdPropertiesKHR);

//                VkMemoryFdPropertiesKHR dmaBufMemoryProperties{};
//                dmaBufMemoryProperties.sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR;
//                dmaBufMemoryProperties.pNext = nullptr;
//                VK_ASSERT_SUCCESS(vkGetMemoryFdPropertiesKHR(dp->vk_device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
//                                                             duppedFd, &dmaBufMemoryProperties));
                // string str = "Fd memory memoryTypeBits: b" + std::bitset<8>(dmaBufMemoryProperties.memoryTypeBits).to_string();
                // COMP_DEBUG_MSG(str);

                VkMemoryRequirements imageMemoryRequirements{};
                vkGetImageMemoryRequirements(dp->vk_device, *vkImage, &imageMemoryRequirements);
                // str = "Image memoryTypeBits: b" +  std::bitset<8>(imageMemoryRequirements.memoryTypeBits).to_string();
                // COMP_DEBUG_MSG(str);

//                const uint32_t bits = dmaBufMemoryProperties.memoryTypeBits & imageMemoryRequirements.memoryTypeBits;
//                assert(bits != 0);

//                const MemoryTypeResult memoryTypeResult =
//                    findMemoryType(dp->vk_physical_device, bits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
                const MemoryTypeResult memoryTypeResult =
                    findMemoryType(dp->vk_physical_device, imageMemoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
                assert(memoryTypeResult.found);
                // str = "Memory type index: " + to_string(memoryTypeResult.typeIndex);
                // COMP_DEBUG_MSG(str);

                VkMemoryDedicatedAllocateInfo dedicatedAllocateInfo{};
                dedicatedAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
                dedicatedAllocateInfo.image = *vkImage;
                VkImportMemoryFdInfoKHR importFdInfo{};
                importFdInfo.sType      = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR;
                importFdInfo.pNext      = &dedicatedAllocateInfo;
                importFdInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
                importFdInfo.fd         = duppedFd;

                // str = "Memory size = " + to_string(imageMemoryRequirements.size);
                // COMP_DEBUG_MSG(str);

                VkMemoryAllocateInfo memoryAllocateInfo{};
                memoryAllocateInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
                memoryAllocateInfo.pNext           = &importFdInfo;
                memoryAllocateInfo.allocationSize  = imageMemoryRequirements.size;
                memoryAllocateInfo.memoryTypeIndex = memoryTypeResult.typeIndex;
                VK_ASSERT_SUCCESS(vkAllocateMemory(dp->vk_device, &memoryAllocateInfo, nullptr, &importedImageMemory));

                VK_ASSERT_SUCCESS(vkBindImageMemory(dp->vk_device, *vkImage, importedImageMemory, 0));
            }
        }
        assert(*vkImage != VK_NULL_HANDLE);

        {
            VK_ASSERT_SUCCESS(vkResetCommandBuffer(blitCB, 0));
            VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};

            VK_ASSERT_SUCCESS(vkBeginCommandBuffer(blitCB, &beginInfo));

            transition_layout(blitCB, *vkImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

            VkImageCopy region{};
            region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.srcSubresource.layerCount = 1;
            region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.dstSubresource.layerCount = 1;
            region.extent.width              = width;
            region.extent.height             = height;
            region.extent.depth              = 1;
            vkCmdCopyImage(blitCB, *vkImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           depth ? buffer_pool->depth_image_pool[ind][eye].image : buffer_pool->image_pool[ind][eye].image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

            transition_layout(blitCB, *vkImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);

            vkEndCommandBuffer(blitCB);

            std::vector<VkCommandBuffer> cmd_bufs = {layout_transition_start_cmd_bufs[ind][eye], blitCB,
                                                     layout_transition_end_cmd_bufs[ind][eye]};
            VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            submitInfo.commandBufferCount = static_cast<uint32_t>(cmd_bufs.size());
            submitInfo.pCommandBuffers    = cmd_bufs.data();

            vulkan::locked_queue_submit(dp->queues[vulkan::queue::GRAPHICS], 1, &submitInfo, nullptr);
//            VK_ASSERT_SUCCESS(vkWaitForFences(dp->vk_device, 1, &blitFence, VK_TRUE, UINT64_MAX));
        }

//        vkDestroyImage(dp->vk_device, vkImage, nullptr);
//        vkFreeMemory(dp->vk_device, importedImageMemory, nullptr);
    }

    void _p_one_iteration() override {
        if (!ready) {
            return;
        }

        auto ind = buffer_pool->src_acquire_image();

        auto decode_end = std::chrono::high_resolution_clock::now();
        for (auto eye = 0; eye < 2; eye++) {
            std::function<void(int)> blit_f = [=](int fd) {
                vkResetFences(dp->vk_device, 1, &blitFence);
                blitTo(ind, eye, fd, false);
            };

            auto ret = color_decoders[eye].dec_capture(blit_f);
            assert(ret == 0 || ret == -EAGAIN);

            if (use_depth) {
                std::function<void(int)> blit_f = [=](int fd) {
                    vkResetFences(dp->vk_device, 1, &blitFence);
                    blitTo(ind, eye, fd, true);
                };

                auto ret = depth_decoders[eye].dec_capture(blit_f);
                assert(ret == 0 || ret == -EAGAIN);
            }
        }

        auto transfer_end = std::chrono::high_resolution_clock::now();
        fast_pose_type decoded_frame_pose;
        {
            std::lock_guard<std::mutex> lock(pose_queue_mutex);
            decoded_frame_pose = pose_queue.front();
            pose_queue.pop();
        }
        buffer_pool->src_release_image(ind, std::move(decoded_frame_pose));

        auto now       = time_point{std::chrono::duration<long, std::nano>{std::chrono::high_resolution_clock::now().time_since_epoch()}};
        auto network_latency   = decoded_frame_pose.predict_target_time - decoded_frame_pose.predict_computed_time;
        auto pipeline_latency  = now - decoded_frame_pose.predict_target_time;
//        log->info("pipeline latency (ms): {}", diff_ns.count() / 1000000.0);
//        log->info("capture (microseconds): {}",
//                  std::chrono::duration_cast<std::chrono::microseconds>(transfer_end - decode_end).count());

        metrics["capture"] += std::chrono::duration_cast<std::chrono::microseconds>(transfer_end - decode_end).count();
        metrics["network"] += std::chrono::duration_cast<std::chrono::microseconds>(network_latency).count();
        metrics["pipeline"] += std::chrono::duration_cast<std::chrono::microseconds>(pipeline_latency).count();

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

    void stop() override {
        running = false;
        decode_q_thread->join();
        threadloop::stop();
    }

private:
    std::shared_ptr<switchboard>                   sb;
    std::shared_ptr<spdlog::logger>                log;
    std::shared_ptr<vulkan::display_provider>      dp;
    switchboard::buffered_reader<compressed_frame> frames_reader;
    switchboard::network_writer<fast_pose_type>    pose_writer;
    std::shared_ptr<pose_prediction>               pp;
    std::atomic<bool>                              ready   = false;
    std::atomic<bool>                              running = true;

    std::shared_ptr<vulkan::buffer_pool<fast_pose_type>> buffer_pool;
    bool                                                 use_depth      = false;
    bool                                                 compare_images = false;
    pose_type                                            fixed_pose;

    std::vector<std::array<VkCommandBuffer, 2>> blit_color_cb;
    std::vector<std::array<VkCommandBuffer, 2>> blit_depth_cb;
    std::vector<std::array<VkCommandBuffer, 2>> layout_transition_start_cmd_bufs;
    std::vector<std::array<VkCommandBuffer, 2>> layout_transition_end_cmd_bufs;

    std::array<mmapi_decoder, 2> color_decoders;
    std::array<mmapi_decoder, 2> depth_decoders;

    std::shared_ptr<const compressed_frame> received_frame;
    std::queue<fast_pose_type>               pose_queue;
    std::mutex                              pose_queue_mutex;

    VkCommandPool command_pool{};

    uint64_t frame_count = 0;
    VkFence  blitFence;

    uint16_t                                       fps_counter    = 0;
    std::chrono::high_resolution_clock::time_point fps_start_time = std::chrono::high_resolution_clock::now();
    std::map<std::string, uint32_t>                metrics;

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

    void push_pose() {
        // Send an identity pose if comparing images
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

    bool network_receive() {
        received_frame = frames_reader.dequeue();
        if (received_frame == nullptr) {
            return false;
        }
        // log->info("Received frame {}", frame_count);
        uint64_t timestamp =
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch())
                .count();
        auto diff_ns = timestamp - received_frame->sent_time;
//        log->info("diff (now - sent_time) (ms): {}", diff_ns / 1000000.0);
        auto pose = received_frame->pose;
        pose.predict_computed_time = time_point{std::chrono::duration<long, std::nano>{received_frame->sent_time}};
        pose.predict_target_time  = time_point{std::chrono::duration<long, std::nano>{timestamp}};
        {
            std::lock_guard<std::mutex> lock(pose_queue_mutex);
            pose_queue.push(pose);
        }
        return true;
    }

    std::shared_ptr<RelativeClock> clock;
    int                            iter = 0;
};

class offload_rendering_client_loader
    : public plugin
    , public vulkan::vk_extension_request {
public:
    offload_rendering_client_loader(const std::string& name, phonebook* pb)
        : plugin(name, pb)
        , offload_rendering_client_plugin{std::make_shared<offload_rendering_client>(name, pb)} {
        pb->register_impl<vulkan::app>(offload_rendering_client_plugin);
        std::cout << "Registered vulkan::timewarp" << std::endl;
    }

    std::vector<const char*> get_required_instance_extensions() override {
        return {VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME, VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME,
                VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME};
    }

    std::vector<const char*> get_required_devices_extensions() override {
        std::vector<const char*> device_extensions;
        device_extensions.push_back(VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME);
        device_extensions.push_back(VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME);
        device_extensions.push_back(VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);
        device_extensions.push_back(VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME);
        device_extensions.push_back(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
        return device_extensions;
    }

    void start() override {
        offload_rendering_client_plugin->start();
    }

    void stop() override {
        offload_rendering_client_plugin->stop();
    }

private:
    std::shared_ptr<offload_rendering_client> offload_rendering_client_plugin;
};

PLUGIN_MAIN(offload_rendering_client_loader)