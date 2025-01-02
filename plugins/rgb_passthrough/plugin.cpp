#include <array>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#define VMA_IMPLEMENTATION
#include "illixr/data_format.hpp"
#include "illixr/global_module_defs.hpp"
#include "illixr/math_util.hpp"
#include "illixr/opencv_data_types.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/pose_prediction.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"
#include "illixr/vk_util/display_sink.hpp"
#include "illixr/vk_util/render_pass.hpp"

#include <opencv2/opencv.hpp>
#include <vulkan/vulkan_core.h>

#define TINYOBJLOADER_IMPLEMENTATION
#include "illixr/gl_util/lib/tiny_obj_loader.h"

#define STB_IMAGE_IMPLEMENTATION
#include "illixr/gl_util/lib/stb_image.h"

#include <unordered_map>

using namespace ILLIXR;

class rgb_passthrough : public app {
public:
    explicit rgb_passthrough(const phonebook* const pb)
        : sb{pb->lookup_impl<switchboard>()}
        , ds{pb->lookup_impl<display_sink>()}
        , _m_cam_reader{sb->get_reader<rgb_depth_type>("rgb_depth")}
        , _m_clock{pb->lookup_impl<RelativeClock>()}
        , appType("passthrough") { }

    void initialize() {
        if (ds->vma_allocator) {
            this->vma_allocator = ds->vma_allocator;
        } else {
            this->vma_allocator = vulkan_utils::create_vma_allocator(ds->vk_instance, ds->vk_physical_device, ds->vk_device);
        }
    }

    void setup(VkRenderPass render_pass, uint32_t subpass) override {
        cv::Mat dummyImage = cv::Mat::ones(display_params::passthrough_height_pixels, display_params::passthrough_width_pixels,
                                           CV_8UC4); // Dummy image for setup
        imageSize          = dummyImage.total() * dummyImage.elemSize();
        for (int i = 0; i < 2; i++) {
            createVmaBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY, stagingBuffers[i],
                            stagingAllocations[i]);
        }
    }

    void createVmaBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage, VkBuffer& buffer,
                         VmaAllocation& allocation) {
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType              = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size               = size;
        bufferInfo.usage              = usage;
        bufferInfo.sharingMode        = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage                   = memoryUsage;

        if (vmaCreateBuffer(ds->vma_allocator, &bufferInfo, &allocInfo, &buffer, &allocation, nullptr) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create VMA buffer.");
        }
    }

    void update_uniforms(const pose_type& fp) override {
        switchboard::ptr<const rgb_depth_type> cam = _m_cam_reader.get_ro_nullable();
        // // If there is not cam data this func call, break early
        if (!cam) {
            return;
        }
        cam_buffer  = cam;
        camImage[0] = cam_buffer->rgb0;
        camImage[1] = cam_buffer->rgb1;
        cv::flip(camImage[0], flippedMat[0], 0);
        cv::flip(camImage[1], flippedMat[1], 0);
    }

    void record_command_buffer(VkCommandBuffer commandBuffer, int eye) override {
        return;
    }

    void record_command_buffer(VkCommandBuffer commandBuffer, VkImage* image, int eye) override {
        if(!cam_buffer) {
            return;
        }
        void* data;
        vmaMapMemory(ds->vma_allocator, stagingAllocations[eye], &data);
        memcpy(data, flippedMat[eye].data, static_cast<size_t>(imageSize));
        vmaUnmapMemory(ds->vma_allocator, stagingAllocations[eye]);
        // Transition image layout to VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
        VkImageMemoryBarrier barrier            = {};
        barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout                       = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout                       = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.image                           = *image;
        barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel   = 0;
        barrier.subresourceRange.levelCount     = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount     = 1;
        barrier.srcAccessMask                   = 0;
        barrier.dstAccessMask                   = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                             nullptr, 1, &barrier);
        // Copy buffer to image
        VkBufferImageCopy region               = {};
        region.bufferOffset                    = 0;
        region.bufferRowLength                 = 0;
        region.bufferImageHeight               = 0;
        region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel       = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount     = 1;
        region.imageOffset                     = {0, 0, 0};
        region.imageExtent = {static_cast<uint32_t>(flippedMat[eye].cols), static_cast<uint32_t>(flippedMat[eye].rows), 1};

        vkCmdCopyBufferToImage(commandBuffer, stagingBuffers[eye], *image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        // Transition image layout to VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                             nullptr, 0, nullptr, 1, &barrier);
    }

    void destroy() override {
        vmaDestroyBuffer(ds->vma_allocator, stagingBuffers[0], stagingAllocations[0]);
        vmaDestroyBuffer(ds->vma_allocator, stagingBuffers[1], stagingAllocations[1]);
    }

    ~rgb_passthrough() override {
        destroy();
    }

    std::string get_app_type() override {
        return appType; // VR or passthrough
    }

private:
    void create_pipeline(VkRenderPass render_pass, uint32_t subpass) { }

    const std::shared_ptr<switchboard>         sb;
    const std::shared_ptr<pose_prediction>     pp;
    const std::shared_ptr<display_sink>        ds = nullptr;
    const std::shared_ptr<const RelativeClock> _m_clock;

    VmaAllocator  vma_allocator{};
    VkCommandPool command_pool{};

    std::string appType = "passthrough";

    switchboard::ptr<const rgb_depth_type> cam_buffer;
    switchboard::reader<rgb_depth_type>    _m_cam_reader;

    std::array<VkBuffer, 2>      stagingBuffers{};
    std::array<VmaAllocation, 2> stagingAllocations{};
    VkDeviceSize                 imageSize;

    std::array<cv::Mat, 2> flippedMat{};
    std::array<cv::Mat, 2> camImage{};
};

class rgb_passthrough_plugin : public plugin {
public:
    rgb_passthrough_plugin(const std::string& name, phonebook* pb)
        : plugin{name, pb}
        , rgb_pt{std::make_shared<rgb_passthrough>(pb)} {
        pb->register_impl<app>(std::static_pointer_cast<rgb_passthrough>(rgb_pt));
    }

    void start() override {
        rgb_pt->initialize();
    }

private:
    std::shared_ptr<rgb_passthrough> rgb_pt;
};

PLUGIN_MAIN(rgb_passthrough_plugin)
