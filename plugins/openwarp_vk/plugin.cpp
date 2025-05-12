#if defined(ILLIXR_MONADO)
    #define VMA_IMPLEMENTATION
#endif

#include "illixr/global_module_defs.hpp"
#include "illixr/math_util.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/data_format/pose_prediction.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"
#include "illixr/vk/display_provider.hpp"
#include "illixr/vk/render_pass.hpp"
#include "illixr/vk/vulkan_utils.hpp"
#include "utils/hmd.hpp"

#include <algorithm>
#include <fstream>
#include <future>
#include <iostream>
#include <mutex>
#include <stack>
#include <vulkan/vulkan.h>

using namespace ILLIXR;

struct DistortionCorrectionVertex {
    glm::vec3 pos;
    glm::vec2 uv0;
    glm::vec2 uv1;
    glm::vec2 uv2;

    static VkVertexInputBindingDescription get_binding_description() {
        VkVertexInputBindingDescription binding_description = {};
        binding_description.binding                         = 0;            // index of the binding in the array of bindings
        binding_description.stride    = sizeof(DistortionCorrectionVertex); // number of bytes from one entry to the next
        binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;        // no instancing

        return binding_description;
    }

    static std::array<VkVertexInputAttributeDescription, 4> get_attribute_descriptions() {
        std::array<VkVertexInputAttributeDescription, 4> attribute_descriptions = {};

        // position
        attribute_descriptions[0].binding  = 0;                          // which binding the per-vertex data comes from
        attribute_descriptions[0].location = 0;                          // location directive of the input in the vertex shader
        attribute_descriptions[0].format   = VK_FORMAT_R32G32B32_SFLOAT; // format of the data
        attribute_descriptions[0].offset =
            offsetof(DistortionCorrectionVertex, pos); // number of bytes since the start of the per-vertex data to read from

        // uv0
        attribute_descriptions[1].binding  = 0;
        attribute_descriptions[1].location = 1;
        attribute_descriptions[1].format   = VK_FORMAT_R32G32_SFLOAT;
        attribute_descriptions[1].offset   = offsetof(DistortionCorrectionVertex, uv0);

        // uv1
        attribute_descriptions[2].binding  = 0;
        attribute_descriptions[2].location = 2;
        attribute_descriptions[2].format   = VK_FORMAT_R32G32_SFLOAT;
        attribute_descriptions[2].offset   = offsetof(DistortionCorrectionVertex, uv1);

        // uv2
        attribute_descriptions[3].binding  = 0;
        attribute_descriptions[3].location = 3;
        attribute_descriptions[3].format   = VK_FORMAT_R32G32_SFLOAT;
        attribute_descriptions[3].offset   = offsetof(DistortionCorrectionVertex, uv2);

        return attribute_descriptions;
    }
};

struct OpenWarpVertex {
    glm::vec3 pos;
    glm::vec2 uv;

    static VkVertexInputBindingDescription get_binding_description() {
        VkVertexInputBindingDescription binding_description = {};
        binding_description.binding                         = 0;     // index of the binding in the array of bindings
        binding_description.stride    = sizeof(OpenWarpVertex);      // number of bytes from one entry to the next
        binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX; // no instancing

        return binding_description;
    }

    static std::array<VkVertexInputAttributeDescription, 2> get_attribute_descriptions() {
        std::array<VkVertexInputAttributeDescription, 2> attribute_descriptions = {};

        // position
        attribute_descriptions[0].binding  = 0;                          // which binding the per-vertex data comes from
        attribute_descriptions[0].location = 0;                          // location directive of the input in the vertex shader
        attribute_descriptions[0].format   = VK_FORMAT_R32G32B32_SFLOAT; // format of the data
        attribute_descriptions[0].offset =
            offsetof(OpenWarpVertex, pos); // number of bytes since the start of the per-vertex data to read from

        // uv
        attribute_descriptions[1].binding  = 0;
        attribute_descriptions[1].location = 1;
        attribute_descriptions[1].format   = VK_FORMAT_R32G32_SFLOAT;
        attribute_descriptions[1].offset   = offsetof(OpenWarpVertex, uv);

        return attribute_descriptions;
    }
};

struct DistortionMatrix {
    glm::mat4 transform;
};

struct WarpMatrices {
    glm::mat4 render_inv_projection[2];
    glm::mat4 render_inv_view[2];
    glm::mat4 warp_view_projection[2];
};

class openwarp_vk : public vulkan::timewarp {
public:
    explicit openwarp_vk(const phonebook* const pb)
        : pb{pb}
        , sb{pb->lookup_impl<switchboard>()}
        , pp{pb->lookup_impl<pose_prediction>()}
        , disable_warp{ILLIXR::str_to_bool(ILLIXR::getenv_or("ILLIXR_TIMEWARP_DISABLE", "False"))} {
        if (std::getenv("ILLIXR_OPENWARP_WIDTH") == nullptr || std::getenv("ILLIXR_OPENWARP_HEIGHT") == nullptr) {
            throw std::runtime_error("Please define ILLIXR_OPENWARP_WIDTH and ILLIXR_OPENWARP_HEIGHT");
        }

        openwarp_width  = std::stoi(std::getenv("ILLIXR_OPENWARP_WIDTH"));
        openwarp_height = std::stoi(std::getenv("ILLIXR_OPENWARP_HEIGHT"));

        using_godot = std::getenv("ILLIXR_USING_GODOT") != nullptr && std::stoi(std::getenv("ILLIXR_USING_GODOT"));
        if (using_godot)
            std::cout << "Using Godot projection matrices!" << std::endl;
        else
            std::cout << "Godot not enabled - defaulting to Unreal projection matrices and reverse Z" << std::endl;
    }

    // For objects that only need to be created a single time and do not need to change.
    void initialize() {
        if (ds->vma_allocator) {
            this->vma_allocator = ds->vma_allocator;
        } else {
            this->vma_allocator = vulkan::create_vma_allocator(ds->vk_instance, ds->vk_physical_device, ds->vk_device);
            deletion_queue.emplace([=]() {
                vmaDestroyAllocator(vma_allocator);
            });
        }

        command_pool   = vulkan::create_command_pool(ds->vk_device, ds->queues[vulkan::queue::queue_type::GRAPHICS].family);
        command_buffer = vulkan::create_command_buffer(ds->vk_device, command_pool);
        deletion_queue.emplace([=]() {
            vkDestroyCommandPool(ds->vk_device, command_pool, nullptr);
        });

        create_descriptor_set_layouts();
        create_uniform_buffers();
        create_texture_sampler();
    }

    void setup(VkRenderPass render_pass, uint32_t subpass, std::shared_ptr<vulkan::buffer_pool<fast_pose_type>> buffer_pool,
               bool input_texture_vulkan_coordinates) override {
        std::lock_guard<std::mutex> lock{m_setup};

        ds = pb->lookup_impl<vulkan::display_provider>();

        swapchain_width  = ds->swapchain_extent.width == 0 ? display_params::width_pixels : ds->swapchain_extent.width;
        swapchain_height = ds->swapchain_extent.height == 0 ? display_params::height_pixels : ds->swapchain_extent.height;

        HMD::GetDefaultHmdInfo(swapchain_width, swapchain_height, display_params::width_meters, display_params::height_meters,
                               display_params::lens_separation, display_params::meters_per_tan_angle,
                               display_params::aberration, hmd_info);

        this->input_texture_vulkan_coordinates = input_texture_vulkan_coordinates;
        if (!initialized) {
            initialize();
            initialized = true;
        } else {
            partial_destroy();
        }

        generate_openwarp_mesh(openwarp_width, openwarp_height);
        generate_distortion_data();

        create_vertex_buffers();
        create_index_buffers();

        this->buffer_pool = std::move(buffer_pool);

        create_descriptor_pool();
        create_openwarp_pipeline();
        distortion_correction_render_pass = render_pass;
        create_distortion_correction_pipeline(render_pass, subpass);

        create_offscreen_images();
        create_descriptor_sets();

        compare_images = std::getenv("ILLIXR_COMPARE_IMAGES") != nullptr && std::stoi(std::getenv("ILLIXR_COMPARE_IMAGES"));
        if (compare_images) {
            // Note that the Quaternion constructor takes the w component first.
            assert(std::getenv("ILLIXR_POSE_FILE") != nullptr);
            std::string pose_filename = std::string(std::getenv("ILLIXR_POSE_FILE"));
            std::cout << "Reading file from " << pose_filename << std::endl;

            std::ifstream pose_file(pose_filename);
            std::string   line;
            while (std::getline(pose_file, line)) {
                float             p_x, p_y, p_z;
                float             q_x, q_y, q_z, q_w;
                std::stringstream ss(line);
                ss >> p_x >> p_y >> p_z >> q_x >> q_y >> q_z >> q_w;

                fixed_poses.emplace_back(time_point(), Eigen::Vector3f(p_x, p_y, p_z), Eigen::Quaternion(q_w, q_x, q_y, q_z));
            }

            std::cout << "Read " << fixed_poses.size() << "poses" << std::endl;

            pose_file.close();
        }
    }

    void partial_destroy() {
        for (int i = 0; i < offscreen_images.size(); i++) {
            vkDestroyFramebuffer(ds->vk_device, offscreen_framebuffers[i], nullptr);
            vkDestroyImageView(ds->vk_device, offscreen_image_views[i], nullptr);
            vmaDestroyImage(vma_allocator, offscreen_images[i], offscreen_image_allocs[i]);

            vkDestroyImageView(ds->vk_device, offscreen_depth_views[i], nullptr);
            vmaDestroyImage(vma_allocator, offscreen_depths[i], offscreen_depth_allocs[i]);
        }

        vkDestroyPipeline(ds->vk_device, openwarp_pipeline, nullptr);
        openwarp_pipeline = VK_NULL_HANDLE;

        vkDestroyPipelineLayout(ds->vk_device, ow_pipeline_layout, nullptr);
        ow_pipeline_layout = VK_NULL_HANDLE;

        vkDestroyPipeline(ds->vk_device, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;

        vkDestroyPipelineLayout(ds->vk_device, dc_pipeline_layout, nullptr);
        dc_pipeline_layout = VK_NULL_HANDLE;

        vkDestroyDescriptorPool(ds->vk_device, descriptor_pool, nullptr);
        descriptor_pool = VK_NULL_HANDLE;
    }

    void update_uniforms(const pose_type& render_pose) override {
        num_update_uniforms_calls++;

        pose_type latest_pose = disable_warp ? render_pose : pp->get_fast_pose().pose;
        if (compare_images) {
            // To be safe, start capturing at 200 frames and wait for 100 frames before trying the next pose.
            // (this should be reflected in the screenshot layer)
            int pose_index = std::clamp(static_cast<int>(frame_count - 150) / 100, 0, static_cast<int>(fixed_poses.size()) - 1);
            latest_pose    = fixed_poses[pose_index];

            std::cout << "Using pose:" << latest_pose.position.x() << " " << latest_pose.position.y() << " "
                      << latest_pose.position.z() << std::endl;
        }

        for (int eye = 0; eye < 2; eye++) {
            Eigen::Matrix4f renderedCameraMatrix = create_camera_matrix(render_pose, eye);
            Eigen::Matrix4f currentCameraMatrix  = create_camera_matrix(latest_pose, eye);

            Eigen::Matrix4f warpVP =
                basicProjection[eye] * currentCameraMatrix.inverse(); // inverse of camera matrix is view matrix

            auto* ow_ubo = (WarpMatrices*) ow_matrices_uniform_alloc_info.pMappedData;
            memcpy(&ow_ubo->render_inv_projection[eye], invProjection[eye].data(), sizeof(Eigen::Matrix4f));
            memcpy(&ow_ubo->render_inv_view[eye], renderedCameraMatrix.data(), sizeof(Eigen::Matrix4f));
            memcpy(&ow_ubo->warp_view_projection[eye], warpVP.data(), sizeof(Eigen::Matrix4f));
        }
    }

    void record_command_buffer(VkCommandBuffer commandBuffer, VkFramebuffer framebuffer, int buffer_ind, bool left) override {
        num_record_calls++;

        if (left)
            frame_count++;

        VkDeviceSize offsets = 0;
        VkClearValue clear_colors[2];
        clear_colors[0].color = {0.0f, 0.0f, 0.0f, 1.0f};

        // Fortunately for us, Godot swapped to reverse Z as of Godot 4.3...
        clear_colors[1].depthStencil.depth = rendering_params::reverse_z ? 0.0 : 1.0;

        // First render OpenWarp offscreen for a distortion correction pass later
        VkRenderPassBeginInfo ow_render_pass_info{};
        ow_render_pass_info.sType                    = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        ow_render_pass_info.renderPass               = openwarp_render_pass;
        ow_render_pass_info.renderArea.offset.x      = 0;
        ow_render_pass_info.renderArea.offset.y      = 0;
        ow_render_pass_info.renderArea.extent.width  = static_cast<uint32_t>(swapchain_width / 2);
        ow_render_pass_info.renderArea.extent.height = static_cast<uint32_t>(swapchain_height);
        ow_render_pass_info.framebuffer              = offscreen_framebuffers[left ? 0 : 1];
        ow_render_pass_info.clearValueCount          = 2;
        ow_render_pass_info.pClearValues             = clear_colors;

        VkViewport ow_viewport{};
        ow_viewport.x        = 0;
        ow_viewport.y        = 0;
        ow_viewport.width    = static_cast<uint32_t>(swapchain_width / 2);
        ow_viewport.height   = static_cast<uint32_t>(swapchain_height);
        ow_viewport.minDepth = 0.0f;
        ow_viewport.maxDepth = 1.0f;

        VkRect2D ow_scissor{};
        ow_scissor.offset.x      = 0;
        ow_scissor.offset.y      = 0;
        ow_scissor.extent.width  = static_cast<uint32_t>(swapchain_width / 2);
        ow_scissor.extent.height = static_cast<uint32_t>(swapchain_height);

        uint32_t eye = static_cast<uint32_t>(left ? 0 : 1);

        vkCmdBeginRenderPass(commandBuffer, &ow_render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdSetViewport(commandBuffer, 0, 1, &ow_viewport);
        vkCmdSetScissor(commandBuffer, 0, 1, &ow_scissor);

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, openwarp_pipeline);
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &ow_vertex_buffer, &offsets);
        vkCmdPushConstants(commandBuffer, ow_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(uint32_t), &eye);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, ow_pipeline_layout, 0, 1,
                                &ow_descriptor_sets[!left][buffer_ind], 0, nullptr);
        vkCmdBindIndexBuffer(commandBuffer, ow_index_buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(commandBuffer, num_openwarp_indices, 1, 0, 0, 0);
        vkCmdEndRenderPass(commandBuffer);

        // Then perform distortion correction to the framebuffer expected by Monado
        VkClearValue clear_color;
        clear_color.color = {0.0f, 0.0f, 0.0f, 1.0f};

        VkRenderPassBeginInfo dc_render_pass_info{};
        dc_render_pass_info.sType                    = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        dc_render_pass_info.renderPass               = distortion_correction_render_pass;
        dc_render_pass_info.renderArea.offset.x      = left ? 0 : static_cast<uint32_t>(swapchain_width / 2);
        dc_render_pass_info.renderArea.offset.y      = 0;
        dc_render_pass_info.renderArea.extent.width  = static_cast<uint32_t>(swapchain_width / 2);
        dc_render_pass_info.renderArea.extent.height = static_cast<uint32_t>(swapchain_height);
        dc_render_pass_info.framebuffer              = framebuffer;
        dc_render_pass_info.clearValueCount          = 1;
        dc_render_pass_info.pClearValues             = &clear_color;

        VkViewport dc_viewport{};
        dc_viewport.x        = left ? 0 : static_cast<uint32_t>(swapchain_width / 2);
        dc_viewport.y        = 0;
        dc_viewport.width    = static_cast<uint32_t>(swapchain_width / 2);
        dc_viewport.height   = static_cast<uint32_t>(swapchain_height);
        dc_viewport.minDepth = 0.0f;
        dc_viewport.maxDepth = 1.0f;

        VkRect2D dc_scissor{};
        dc_scissor.offset.x      = left ? 0 : static_cast<uint32_t>(swapchain_width / 2);
        dc_scissor.offset.y      = 0;
        dc_scissor.extent.width  = static_cast<uint32_t>(swapchain_width / 2);
        dc_scissor.extent.height = static_cast<uint32_t>(swapchain_height);

        vkCmdBeginRenderPass(commandBuffer, &dc_render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdSetViewport(commandBuffer, 0, 1, &dc_viewport);
        vkCmdSetScissor(commandBuffer, 0, 1, &dc_scissor);

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &dc_vertex_buffer, &offsets);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, dc_pipeline_layout, 0, 1,
                                &dc_descriptor_sets[!left][0], 0, nullptr);
        vkCmdBindIndexBuffer(commandBuffer, dc_index_buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(commandBuffer, num_distortion_indices, 1, 0, static_cast<int>(num_distortion_vertices * !left), 0);
        vkCmdEndRenderPass(commandBuffer);
    }

    bool is_external() override {
        return false;
    }

    void destroy() override {
        partial_destroy();
        // drain deletion_queue
        while (!deletion_queue.empty()) {
            deletion_queue.top()();
            deletion_queue.pop();
        }
    }

private:
    void create_offscreen_images() {
        for (int eye = 0; eye < 2; eye++) {
            VkImageCreateInfo image_info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
            image_info.imageType         = VK_IMAGE_TYPE_2D;
            image_info.extent.width      = swapchain_width / 2;
            image_info.extent.height     = swapchain_height;
            image_info.extent.depth      = 1;
            image_info.mipLevels         = 1;
            image_info.arrayLayers       = 1;
            image_info.format            = VK_FORMAT_R8G8B8A8_UNORM;
            image_info.tiling            = VK_IMAGE_TILING_OPTIMAL;
            image_info.initialLayout     = VK_IMAGE_LAYOUT_UNDEFINED;
            image_info.usage             = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
            image_info.samples           = VK_SAMPLE_COUNT_1_BIT;

            VmaAllocationCreateInfo create_info = {};
            create_info.usage                   = VMA_MEMORY_USAGE_AUTO;
            create_info.flags                   = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
            create_info.priority                = 1.0f;

            VK_ASSERT_SUCCESS(vmaCreateImage(vma_allocator, &image_info, &create_info, &offscreen_images[eye],
                                             &offscreen_image_allocs[eye], nullptr));

            VkImageViewCreateInfo view_info           = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            view_info.image                           = offscreen_images[eye];
            view_info.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
            view_info.format                          = VK_FORMAT_R8G8B8A8_UNORM;
            view_info.components.r                    = VK_COMPONENT_SWIZZLE_IDENTITY;
            view_info.components.g                    = VK_COMPONENT_SWIZZLE_IDENTITY;
            view_info.components.b                    = VK_COMPONENT_SWIZZLE_IDENTITY;
            view_info.components.a                    = VK_COMPONENT_SWIZZLE_IDENTITY;
            view_info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            view_info.subresourceRange.baseMipLevel   = 0;
            view_info.subresourceRange.levelCount     = 1;
            view_info.subresourceRange.baseArrayLayer = 0;
            view_info.subresourceRange.layerCount     = 1;

            VK_ASSERT_SUCCESS(vkCreateImageView(ds->vk_device, &view_info, nullptr, &offscreen_image_views[eye]));

            VkImageCreateInfo depth_image_info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
            depth_image_info.imageType         = VK_IMAGE_TYPE_2D;
            depth_image_info.extent.width      = swapchain_width / 2;
            depth_image_info.extent.height     = swapchain_height;
            depth_image_info.extent.depth      = 1;
            depth_image_info.mipLevels         = 1;
            depth_image_info.arrayLayers       = 1;
            depth_image_info.format            = VK_FORMAT_D16_UNORM;
            depth_image_info.tiling            = VK_IMAGE_TILING_OPTIMAL;
            depth_image_info.initialLayout     = VK_IMAGE_LAYOUT_UNDEFINED;
            depth_image_info.usage             = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            depth_image_info.samples           = VK_SAMPLE_COUNT_1_BIT;

            VmaAllocationCreateInfo depth_create_info = {};
            depth_create_info.usage                   = VMA_MEMORY_USAGE_AUTO;
            depth_create_info.flags                   = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
            depth_create_info.priority                = 1.0f;

            VK_ASSERT_SUCCESS(vmaCreateImage(vma_allocator, &depth_image_info, &depth_create_info, &offscreen_depths[eye],
                                             &offscreen_depth_allocs[eye], nullptr));

            VkImageViewCreateInfo depth_view_info           = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            depth_view_info.image                           = offscreen_depths[eye];
            depth_view_info.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
            depth_view_info.format                          = VK_FORMAT_D16_UNORM;
            depth_view_info.components.r                    = VK_COMPONENT_SWIZZLE_IDENTITY;
            depth_view_info.components.g                    = VK_COMPONENT_SWIZZLE_IDENTITY;
            depth_view_info.components.b                    = VK_COMPONENT_SWIZZLE_IDENTITY;
            depth_view_info.components.a                    = VK_COMPONENT_SWIZZLE_IDENTITY;
            depth_view_info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT;
            depth_view_info.subresourceRange.baseMipLevel   = 0;
            depth_view_info.subresourceRange.levelCount     = 1;
            depth_view_info.subresourceRange.baseArrayLayer = 0;
            depth_view_info.subresourceRange.layerCount     = 1;

            VK_ASSERT_SUCCESS(vkCreateImageView(ds->vk_device, &depth_view_info, nullptr, &offscreen_depth_views[eye]));

            VkImageView attachments[2] = {offscreen_image_views[eye], offscreen_depth_views[eye]};

            // Need a framebuffer to render to
            VkFramebufferCreateInfo framebuffer_info = {
                .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                .renderPass      = openwarp_render_pass,
                .attachmentCount = 2,
                .pAttachments    = attachments,
                .width           = swapchain_width / 2,
                .height          = swapchain_height,
                .layers          = 1,
            };

            VK_ASSERT_SUCCESS(vkCreateFramebuffer(ds->vk_device, &framebuffer_info, nullptr, &offscreen_framebuffers[eye]));
        }
    }

    void create_vertex_buffers() {
        // OpenWarp Vertices
        VkBufferCreateInfo ow_staging_buffer_info = {
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, // sType
            nullptr,                              // pNext
            0,                                    // flags
            0,                                    // size
            0,                                    // usage
            {},                                   // sharingMode
            0,                                    // queueFamilyIndexCount
            nullptr                               // pQueueFamilyIndices
        };
        ow_staging_buffer_info.size  = sizeof(OpenWarpVertex) * num_openwarp_vertices;
        ow_staging_buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

        VmaAllocationCreateInfo ow_staging_alloc_info = {};
        ow_staging_alloc_info.usage                   = VMA_MEMORY_USAGE_AUTO;
        ow_staging_alloc_info.flags                   = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

        VkBuffer      ow_staging_buffer;
        VmaAllocation ow_staging_alloc;
        VK_ASSERT_SUCCESS(vmaCreateBuffer(vma_allocator, &ow_staging_buffer_info, &ow_staging_alloc_info, &ow_staging_buffer,
                                          &ow_staging_alloc, nullptr))

        VkBufferCreateInfo ow_buffer_info = {
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, // sType
            nullptr,                              // pNext
            0,                                    // flags
            0,                                    // size
            0,                                    // usage
            {},                                   // sharingMode
            0,                                    // queueFamilyIndexCount
            nullptr                               // pQueueFamilyIndices
        };
        ow_buffer_info.size  = sizeof(OpenWarpVertex) * num_openwarp_vertices;
        ow_buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

        VmaAllocationCreateInfo ow_alloc_info = {};
        ow_alloc_info.usage                   = VMA_MEMORY_USAGE_GPU_ONLY;

        VmaAllocation ow_vertex_alloc;
        VK_ASSERT_SUCCESS(
            vmaCreateBuffer(vma_allocator, &ow_buffer_info, &ow_alloc_info, &ow_vertex_buffer, &ow_vertex_alloc, nullptr))

        void* ow_mapped_data;
        VK_ASSERT_SUCCESS(vmaMapMemory(vma_allocator, ow_staging_alloc, &ow_mapped_data))
        memcpy(ow_mapped_data, openwarp_vertices.data(), sizeof(OpenWarpVertex) * num_openwarp_vertices);
        vmaUnmapMemory(vma_allocator, ow_staging_alloc);

        VkCommandBuffer ow_command_buffer_local = vulkan::begin_one_time_command(ds->vk_device, command_pool);
        VkBufferCopy    ow_copy_region          = {};
        ow_copy_region.size                     = sizeof(OpenWarpVertex) * num_openwarp_vertices;
        vkCmdCopyBuffer(ow_command_buffer_local, ow_staging_buffer, ow_vertex_buffer, 1, &ow_copy_region);
        vulkan::end_one_time_command(ds->vk_device, command_pool, ds->queues[vulkan::queue::queue_type::GRAPHICS],
                                     ow_command_buffer_local);

        vmaDestroyBuffer(vma_allocator, ow_staging_buffer, ow_staging_alloc);

        deletion_queue.emplace([=]() {
            vmaDestroyBuffer(vma_allocator, ow_vertex_buffer, ow_vertex_alloc);
        });

        // Distortion Correction Vertices
        VkBufferCreateInfo dc_staging_buffer_info = {
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, // sType
            nullptr,                              // pNext
            0,                                    // flags
            0,                                    // size
            0,                                    // usage
            {},                                   // sharingMode
            0,                                    // queueFamilyIndexCount
            nullptr                               // pQueueFamilyIndices
        };
        dc_staging_buffer_info.size  = sizeof(DistortionCorrectionVertex) * num_distortion_vertices * HMD::NUM_EYES;
        dc_staging_buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

        VmaAllocationCreateInfo dc_staging_alloc_info = {};
        dc_staging_alloc_info.usage                   = VMA_MEMORY_USAGE_AUTO;
        dc_staging_alloc_info.flags                   = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

        VkBuffer      dc_staging_buffer;
        VmaAllocation dc_staging_alloc;
        VK_ASSERT_SUCCESS(vmaCreateBuffer(vma_allocator, &dc_staging_buffer_info, &dc_staging_alloc_info, &dc_staging_buffer,
                                          &dc_staging_alloc, nullptr))

        VkBufferCreateInfo dc_buffer_info = {
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, // sType
            nullptr,                              // pNext
            0,                                    // flags
            0,                                    // size
            0,                                    // usage
            {},                                   // sharingMode
            0,                                    // queueFamilyIndexCount
            nullptr                               // pQueueFamilyIndices
        };
        dc_buffer_info.size  = sizeof(DistortionCorrectionVertex) * num_distortion_vertices * HMD::NUM_EYES;
        dc_buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

        VmaAllocationCreateInfo dc_alloc_info = {};
        dc_alloc_info.usage                   = VMA_MEMORY_USAGE_GPU_ONLY;

        VmaAllocation dc_vertex_alloc;
        VK_ASSERT_SUCCESS(
            vmaCreateBuffer(vma_allocator, &dc_buffer_info, &dc_alloc_info, &dc_vertex_buffer, &dc_vertex_alloc, nullptr))

        void* dc_mapped_data;
        VK_ASSERT_SUCCESS(vmaMapMemory(vma_allocator, dc_staging_alloc, &dc_mapped_data))
        memcpy(dc_mapped_data, distortion_vertices.data(),
               sizeof(DistortionCorrectionVertex) * num_distortion_vertices * HMD::NUM_EYES);
        vmaUnmapMemory(vma_allocator, dc_staging_alloc);

        VkCommandBuffer dc_command_buffer_local = vulkan::begin_one_time_command(ds->vk_device, command_pool);
        VkBufferCopy    dc_copy_region          = {};
        dc_copy_region.size                     = sizeof(DistortionCorrectionVertex) * num_distortion_vertices * HMD::NUM_EYES;
        vkCmdCopyBuffer(dc_command_buffer_local, dc_staging_buffer, dc_vertex_buffer, 1, &dc_copy_region);
        vulkan::end_one_time_command(ds->vk_device, command_pool, ds->queues[vulkan::queue::queue_type::GRAPHICS],
                                     dc_command_buffer_local);

        vmaDestroyBuffer(vma_allocator, dc_staging_buffer, dc_staging_alloc);

        deletion_queue.emplace([=]() {
            vmaDestroyBuffer(vma_allocator, dc_vertex_buffer, dc_vertex_alloc);
        });
    }

    void create_index_buffers() {
        // OpenWarp index buffer
        VkBufferCreateInfo ow_staging_buffer_info = {
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, // sType
            nullptr,                              // pNext
            0,                                    // flags
            0,                                    // size
            0,                                    // usage
            {},                                   // sharingMode
            0,                                    // queueFamilyIndexCount
            nullptr                               // pQueueFamilyIndices
        };
        ow_staging_buffer_info.size  = sizeof(uint32_t) * num_openwarp_indices;
        ow_staging_buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

        VmaAllocationCreateInfo ow_staging_alloc_info = {};
        ow_staging_alloc_info.usage                   = VMA_MEMORY_USAGE_AUTO;
        ow_staging_alloc_info.flags                   = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

        VkBuffer      ow_staging_buffer;
        VmaAllocation ow_staging_alloc;
        VK_ASSERT_SUCCESS(vmaCreateBuffer(vma_allocator, &ow_staging_buffer_info, &ow_staging_alloc_info, &ow_staging_buffer,
                                          &ow_staging_alloc, nullptr))

        VkBufferCreateInfo ow_buffer_info = {
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, // sType
            nullptr,                              // pNext
            0,                                    // flags
            0,                                    // size
            0,                                    // usage
            {},                                   // sharingMode
            0,                                    // queueFamilyIndexCount
            nullptr                               // pQueueFamilyIndices
        };
        ow_buffer_info.size  = sizeof(uint32_t) * num_openwarp_indices;
        ow_buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

        VmaAllocationCreateInfo ow_alloc_info = {};
        ow_alloc_info.usage                   = VMA_MEMORY_USAGE_GPU_ONLY;

        VmaAllocation ow_index_alloc;
        VK_ASSERT_SUCCESS(
            vmaCreateBuffer(vma_allocator, &ow_buffer_info, &ow_alloc_info, &ow_index_buffer, &ow_index_alloc, nullptr))

        void* ow_mapped_data;
        VK_ASSERT_SUCCESS(vmaMapMemory(vma_allocator, ow_staging_alloc, &ow_mapped_data))
        memcpy(ow_mapped_data, openwarp_indices.data(), sizeof(uint32_t) * num_openwarp_indices);
        vmaUnmapMemory(vma_allocator, ow_staging_alloc);

        VkCommandBuffer ow_command_buffer_local = vulkan::begin_one_time_command(ds->vk_device, command_pool);
        VkBufferCopy    ow_copy_region          = {};
        ow_copy_region.size                     = sizeof(uint32_t) * num_openwarp_indices;
        vkCmdCopyBuffer(ow_command_buffer_local, ow_staging_buffer, ow_index_buffer, 1, &ow_copy_region);
        vulkan::end_one_time_command(ds->vk_device, command_pool, ds->queues[vulkan::queue::queue_type::GRAPHICS],
                                     ow_command_buffer_local);

        vmaDestroyBuffer(vma_allocator, ow_staging_buffer, ow_staging_alloc);

        deletion_queue.emplace([=]() {
            vmaDestroyBuffer(vma_allocator, dc_index_buffer, ow_index_alloc);
        });

        // Distortion correction index buffer
        VkBufferCreateInfo dc_staging_buffer_info = {
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, // sType
            nullptr,                              // pNext
            0,                                    // flags
            0,                                    // size
            0,                                    // usage
            {},                                   // sharingMode
            0,                                    // queueFamilyIndexCount
            nullptr                               // pQueueFamilyIndices
        };
        dc_staging_buffer_info.size  = sizeof(uint32_t) * num_distortion_indices;
        dc_staging_buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

        VmaAllocationCreateInfo dc_staging_alloc_info = {};
        dc_staging_alloc_info.usage                   = VMA_MEMORY_USAGE_AUTO;
        dc_staging_alloc_info.flags                   = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

        VkBuffer      dc_staging_buffer;
        VmaAllocation dc_staging_alloc;
        VK_ASSERT_SUCCESS(vmaCreateBuffer(vma_allocator, &dc_staging_buffer_info, &dc_staging_alloc_info, &dc_staging_buffer,
                                          &dc_staging_alloc, nullptr))

        VkBufferCreateInfo dc_buffer_info = {
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, // sType
            nullptr,                              // pNext
            0,                                    // flags
            0,                                    // size
            0,                                    // usage
            {},                                   // sharingMode
            0,                                    // queueFamilyIndexCount
            nullptr                               // pQueueFamilyIndices
        };
        dc_buffer_info.size  = sizeof(uint32_t) * num_distortion_indices;
        dc_buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

        VmaAllocationCreateInfo dc_alloc_info = {};
        dc_alloc_info.usage                   = VMA_MEMORY_USAGE_GPU_ONLY;

        VmaAllocation dc_index_alloc;
        VK_ASSERT_SUCCESS(
            vmaCreateBuffer(vma_allocator, &dc_buffer_info, &dc_alloc_info, &dc_index_buffer, &dc_index_alloc, nullptr))

        void* dc_mapped_data;
        VK_ASSERT_SUCCESS(vmaMapMemory(vma_allocator, dc_staging_alloc, &dc_mapped_data))
        memcpy(dc_mapped_data, distortion_indices.data(), sizeof(uint32_t) * num_distortion_indices);
        vmaUnmapMemory(vma_allocator, dc_staging_alloc);

        VkCommandBuffer dc_command_buffer_local = vulkan::begin_one_time_command(ds->vk_device, command_pool);
        VkBufferCopy    dc_copy_region          = {};
        dc_copy_region.size                     = sizeof(uint32_t) * num_distortion_indices;
        vkCmdCopyBuffer(dc_command_buffer_local, dc_staging_buffer, dc_index_buffer, 1, &dc_copy_region);
        vulkan::end_one_time_command(ds->vk_device, command_pool, ds->queues[vulkan::queue::queue_type::GRAPHICS],
                                     dc_command_buffer_local);

        vmaDestroyBuffer(vma_allocator, dc_staging_buffer, dc_staging_alloc);

        deletion_queue.emplace([=]() {
            vmaDestroyBuffer(vma_allocator, dc_index_buffer, dc_index_alloc);
        });
    }

    void generate_distortion_data() {
        // Calculate the number of vertices+indices in the distortion mesh.
        num_distortion_vertices = (hmd_info.eyeTilesHigh + 1) * (hmd_info.eyeTilesWide + 1);
        num_distortion_indices  = hmd_info.eyeTilesHigh * hmd_info.eyeTilesWide * 6;

        // Allocate memory for the elements/indices array.
        distortion_indices.resize(num_distortion_indices);

        // This is just a simple grid/plane index array, nothing fancy.
        // Same for both eye distortions, too!
        for (int y = 0; y < hmd_info.eyeTilesHigh; y++) {
            for (int x = 0; x < hmd_info.eyeTilesWide; x++) {
                const int offset = (y * hmd_info.eyeTilesWide + x) * 6;

                distortion_indices[offset + 0] = ((y + 0) * (hmd_info.eyeTilesWide + 1) + (x + 0));
                distortion_indices[offset + 1] = ((y + 1) * (hmd_info.eyeTilesWide + 1) + (x + 0));
                distortion_indices[offset + 2] = ((y + 0) * (hmd_info.eyeTilesWide + 1) + (x + 1));

                distortion_indices[offset + 3] = ((y + 0) * (hmd_info.eyeTilesWide + 1) + (x + 1));
                distortion_indices[offset + 4] = ((y + 1) * (hmd_info.eyeTilesWide + 1) + (x + 0));
                distortion_indices[offset + 5] = ((y + 1) * (hmd_info.eyeTilesWide + 1) + (x + 1));
            }
        }

        // There are `num_distortion_vertices` distortion coordinates for each color channel (3) of each eye (2).
        // These are NOT the coordinates of the distorted vertices. They are *coefficients* that will be used to
        // offset the UV coordinates of the distortion mesh.
        std::array<std::array<std::vector<HMD::mesh_coord2d_t>, HMD::NUM_COLOR_CHANNELS>, HMD::NUM_EYES> distort_coords;
        for (auto& eye_coords : distort_coords) {
            for (auto& channel_coords : eye_coords) {
                channel_coords.resize(num_distortion_vertices);
            }
        }
        HMD::BuildDistortionMeshes(distort_coords, hmd_info);

        // Allocate memory for position and UV CPU buffers.
        const std::size_t num_elems_pos_uv = HMD::NUM_EYES * num_distortion_vertices;
        distortion_vertices.resize(num_elems_pos_uv);

        // Construct perspective projection matrix
        // math_util::projection_fov(&basicProjection, display_params::fov_x / 2.0f, display_params::fov_x / 2.0f,
        //                           display_params::fov_y / 2.0f, display_params::fov_y / 2.0f, rendering_params::near_z,
        //                           rendering_params::far_z, rendering_params::reverse_z);
        // invProjection = basicProjection.inverse();

        float near_z = rendering_params::near_z;
        float far_z  = rendering_params::far_z;

        // Hacked in projection matrix from Unreal and hardcoded values
        for (int eye = 0; eye < 2; eye++) {
            math_util::unreal_projection(&basicProjection[eye], index_params::fov_left[eye], index_params::fov_right[eye],
                                         index_params::fov_up[eye], index_params::fov_down[eye]);

            float scale = 1.0f;
            if (std::getenv("ILLIXR_OVERSCAN") != nullptr) {
                scale = std::stof(std::getenv("ILLIXR_OVERSCAN"));
            }
            float tan_left  = server_params::fov_left[eye];
            float tan_right = server_params::fov_right[eye];
            float tan_up    = server_params::fov_up[eye];
            float tan_down  = server_params::fov_down[eye];
            float fov_left  = std::atan(tan_left);
            float fov_right = std::atan(tan_right);
            float fov_up    = std::atan(tan_up);
            float fov_down  = std::atan(tan_down);
            tan_left        = std::tan(fov_left * scale);
            tan_right       = std::tan(fov_right * scale);
            tan_up          = std::tan(fov_up * scale);
            tan_down        = std::tan(fov_down * scale);

            // The server can render at a larger FoV, so the inverse should account for that.
            // The FOVs provided to the server should match the ones provided to Monado.
            Eigen::Matrix4f server_fov;
            if (using_godot) {
                math_util::godot_projection(&basicProjection[eye], index_params::fov_left[eye], index_params::fov_right[eye],
                                            index_params::fov_up[eye], index_params::fov_down[eye]);
                math_util::godot_projection(&server_fov, tan_left, tan_right, tan_up, tan_down);
            } else {
                math_util::unreal_projection(&basicProjection[eye], index_params::fov_left[eye], index_params::fov_right[eye],
                                             index_params::fov_up[eye], index_params::fov_down[eye]);
                math_util::unreal_projection(&server_fov, tan_left, tan_right, tan_up, tan_down);
            }

            invProjection[eye] = server_fov.inverse();
        }

        for (int eye = 0; eye < HMD::NUM_EYES; eye++) {
            Eigen::Matrix4f distortion_matrix = calculate_distortion_transform(basicProjection[eye]);
            for (int y = 0; y <= hmd_info.eyeTilesHigh; y++) {
                for (int x = 0; x <= hmd_info.eyeTilesWide; x++) {
                    const int index = y * (hmd_info.eyeTilesWide + 1) + x;

                    // Set the physical distortion mesh coordinates. These are rectangular/gridlike, not distorted.
                    // The distortion is handled by the UVs, not the actual mesh coordinates!
                    // distortion_positions[eye * num_distortion_vertices + index].x = (-1.0f + eye + ((float) x /
                    // hmd_info.eyeTilesWide));
                    distortion_vertices[eye * num_distortion_vertices + index].pos.x =
                        (-1.0f + 2 * (static_cast<float>(x) / static_cast<float>(hmd_info.eyeTilesWide)));

                    // flip the y coordinates for Vulkan texture
                    distortion_vertices[eye * num_distortion_vertices + index].pos.y =
                        // (input_texture_vulkan_coordinates ? -1.0f : 1.0f) *
                        (-1.0f +
                         2.0f * (static_cast<float>(hmd_info.eyeTilesHigh - y) / static_cast<float>(hmd_info.eyeTilesHigh)) *
                             (static_cast<float>(hmd_info.eyeTilesHigh * hmd_info.tilePixelsHigh) /
                              static_cast<float>(hmd_info.displayPixelsHigh)));
                    distortion_vertices[eye * num_distortion_vertices + index].pos.z = 0.0f;

                    // Use the previously-calculated distort_coords to set the UVs on the distortion mesh
                    Eigen::Vector4f vertex_uv0(distort_coords[eye][0][index].x, distort_coords[eye][0][index].y, -1, 1);
                    Eigen::Vector4f vertex_uv1(distort_coords[eye][1][index].x, distort_coords[eye][1][index].y, -1, 1);
                    Eigen::Vector4f vertex_uv2(distort_coords[eye][2][index].x, distort_coords[eye][2][index].y, -1, 1);

                    Eigen::Vector4f uv0 = distortion_matrix * vertex_uv0;
                    Eigen::Vector4f uv1 = distortion_matrix * vertex_uv1;
                    Eigen::Vector4f uv2 = distortion_matrix * vertex_uv2;

                    float factor0 = 1.0f / std::max(uv0.z(), 0.00001f);
                    float factor1 = 1.0f / std::max(uv1.z(), 0.00001f);
                    float factor2 = 1.0f / std::max(uv2.z(), 0.00001f);

                    distortion_vertices[eye * num_distortion_vertices + index].uv0.x = uv0.x() * factor0;
                    distortion_vertices[eye * num_distortion_vertices + index].uv0.y = uv0.y() * factor0;
                    distortion_vertices[eye * num_distortion_vertices + index].uv1.x = uv1.x() * factor1;
                    distortion_vertices[eye * num_distortion_vertices + index].uv1.y = uv1.y() * factor1;
                    distortion_vertices[eye * num_distortion_vertices + index].uv2.x = uv2.x() * factor2;
                    distortion_vertices[eye * num_distortion_vertices + index].uv2.y = uv2.y() * factor2;
                }
            }
        }
    }

    void generate_openwarp_mesh(size_t width, size_t height) {
        std::cout << "Generating reprojection mesh, size (" << width << ", " << height << ")" << std::endl;

        // width and height are not in # of verts, but in # of faces.
        num_openwarp_indices  = 2 * 3 * width * height;
        num_openwarp_vertices = (width + 1) * (height + 1);

        // Size the vectors accordingly
        openwarp_indices.resize(num_openwarp_indices);
        openwarp_vertices.resize(num_openwarp_vertices);

        // Build indices.
        for (size_t y = 0; y < height; y++) {
            for (size_t x = 0; x < width; x++) {
                const int offset = (y * width + x) * 6;

                openwarp_indices[offset + 0] = (GLuint) ((y + 0) * (width + 1) + (x + 0));
                openwarp_indices[offset + 1] = (GLuint) ((y + 1) * (width + 1) + (x + 0));
                openwarp_indices[offset + 2] = (GLuint) ((y + 0) * (width + 1) + (x + 1));

                openwarp_indices[offset + 3] = (GLuint) ((y + 0) * (width + 1) + (x + 1));
                openwarp_indices[offset + 4] = (GLuint) ((y + 1) * (width + 1) + (x + 0));
                openwarp_indices[offset + 5] = (GLuint) ((y + 1) * (width + 1) + (x + 1));
            }
        }

        // Build vertices
        for (size_t y = 0; y < height + 1; y++) {
            for (size_t x = 0; x < width + 1; x++) {
                size_t index = y * (width + 1) + x;

                openwarp_vertices[index].uv.x = ((float) x / width);
                openwarp_vertices[index].uv.y = (height - (float) y) / height;

                if (x == 0) {
                    openwarp_vertices[index].uv.x = -0.5f;
                }
                if (x == width) {
                    openwarp_vertices[index].uv.x = 1.5f;
                }

                if (y == 0) {
                    openwarp_vertices[index].uv.y = 1.5f;
                }
                if (y == height) {
                    openwarp_vertices[index].uv.y = -0.5f;
                }
            }
        }
    }

    void create_texture_sampler() {
        VkSamplerCreateInfo samplerInfo = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        samplerInfo.magFilter           = VK_FILTER_LINEAR; // how to interpolate texels that are magnified on screen
        samplerInfo.minFilter           = VK_FILTER_LINEAR;

        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        samplerInfo.borderColor  = VK_BORDER_COLOR_INT_OPAQUE_BLACK; // black outside the texture

        samplerInfo.anisotropyEnable        = VK_FALSE;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable           = VK_FALSE;
        samplerInfo.compareOp               = VK_COMPARE_OP_ALWAYS;

        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.mipLodBias = 0.f;
        samplerInfo.minLod     = 0.f;
        samplerInfo.maxLod     = 0.f;

        VK_ASSERT_SUCCESS(vkCreateSampler(ds->vk_device, &samplerInfo, nullptr, &fb_sampler))

        deletion_queue.emplace([=]() {
            vkDestroySampler(ds->vk_device, fb_sampler, nullptr);
        });

        VK_ASSERT_SUCCESS(vkCreateSampler(ds->vk_device, &samplerInfo, nullptr, &fb_sampler))

        deletion_queue.emplace([=]() {
            vkDestroySampler(ds->vk_device, fb_sampler, nullptr);
        });
    }

    void create_descriptor_set_layouts() {
        // OpenWarp descriptor set
        VkDescriptorSetLayoutBinding imageLayoutBinding = {};
        imageLayoutBinding.binding                      = 0;
        imageLayoutBinding.descriptorType               = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        imageLayoutBinding.descriptorCount              = 1;
        imageLayoutBinding.stageFlags                   = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutBinding depthLayoutBinding = {};
        depthLayoutBinding.binding                      = 1;
        depthLayoutBinding.descriptorType               = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        depthLayoutBinding.descriptorCount              = 1;
        depthLayoutBinding.stageFlags                   = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        // depthLayoutBinding.stageFlags                   = VK_SHADER_STAGE_VERTEX_BIT;

        VkDescriptorSetLayoutBinding matrixUboLayoutBinding = {};
        matrixUboLayoutBinding.binding                      = 2;
        matrixUboLayoutBinding.descriptorType               = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        matrixUboLayoutBinding.descriptorCount              = 1;
        matrixUboLayoutBinding.stageFlags                   = VK_SHADER_STAGE_VERTEX_BIT;

        std::array<VkDescriptorSetLayoutBinding, 3> ow_bindings    = {imageLayoutBinding, depthLayoutBinding,
                                                                      matrixUboLayoutBinding};
        VkDescriptorSetLayoutCreateInfo             ow_layout_info = {};
        ow_layout_info.sType                                       = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        ow_layout_info.bindingCount                                = static_cast<uint32_t>(ow_bindings.size());
        ow_layout_info.pBindings = ow_bindings.data(); // array of VkDescriptorSetLayoutBinding structs

        VK_ASSERT_SUCCESS(vkCreateDescriptorSetLayout(ds->vk_device, &ow_layout_info, nullptr, &ow_descriptor_set_layout))
        deletion_queue.emplace([=]() {
            vkDestroyDescriptorSetLayout(ds->vk_device, ow_descriptor_set_layout, nullptr);
        });

        // Distortion correction descriptor set
        VkDescriptorSetLayoutBinding offscreenImageLayoutBinding = {};
        offscreenImageLayoutBinding.binding                      = 0; // binding number in the shader
        offscreenImageLayoutBinding.descriptorType               = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        offscreenImageLayoutBinding.descriptorCount              = 1;
        offscreenImageLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT; // shader stages that can access the descriptor

        std::array<VkDescriptorSetLayoutBinding, 1> dc_bindings    = {offscreenImageLayoutBinding};
        VkDescriptorSetLayoutCreateInfo             dc_layout_info = {};
        dc_layout_info.sType                                       = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dc_layout_info.bindingCount                                = static_cast<uint32_t>(dc_bindings.size());
        dc_layout_info.pBindings = dc_bindings.data(); // array of VkDescriptorSetLayoutBinding structs

        VK_ASSERT_SUCCESS(vkCreateDescriptorSetLayout(ds->vk_device, &dc_layout_info, nullptr, &dc_descriptor_set_layout))
        deletion_queue.emplace([=]() {
            vkDestroyDescriptorSetLayout(ds->vk_device, dc_descriptor_set_layout, nullptr);
        });
    }

    void create_uniform_buffers() {
        // Matrix data
        VkBufferCreateInfo matrixBufferInfo = {
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, // sType
            nullptr,                              // pNext
            0,                                    // flags
            0,                                    // size
            0,                                    // usage
            {},                                   // sharingMode
            0,                                    // queueFamilyIndexCount
            nullptr                               // pQueueFamilyIndices
        };
        matrixBufferInfo.size  = sizeof(WarpMatrices);
        matrixBufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

        VmaAllocationCreateInfo createInfo = {};
        createInfo.usage                   = VMA_MEMORY_USAGE_AUTO;
        createInfo.flags         = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
        createInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

        VK_ASSERT_SUCCESS(vmaCreateBuffer(vma_allocator, &matrixBufferInfo, &createInfo, &ow_matrices_uniform_buffer,
                                          &ow_matrices_uniform_alloc, &ow_matrices_uniform_alloc_info))
        deletion_queue.emplace([=]() {
            vmaDestroyBuffer(vma_allocator, ow_matrices_uniform_buffer, ow_matrices_uniform_alloc);
        });
    }

    void create_descriptor_pool() {
        std::array<VkDescriptorPoolSize, 2> poolSizes = {};
        std::cout << buffer_pool->image_pool.size() << std::endl;
        poolSizes[0].type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSizes[0].descriptorCount = buffer_pool->image_pool.size() * 2;
        poolSizes[1].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSizes[1].descriptorCount = (2 * buffer_pool->image_pool.size() + 1) * 2;

        VkDescriptorPoolCreateInfo poolInfo = {
            VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, // sType
            nullptr,                                       // pNext
            0,                                             // flags
            0,                                             // maxSets
            0,                                             // poolSizeCount
            nullptr                                        // pPoolSizes
        };
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes    = poolSizes.data();
        poolInfo.maxSets       = (buffer_pool->image_pool.size() + 1) * 2;

        VK_ASSERT_SUCCESS(vkCreateDescriptorPool(ds->vk_device, &poolInfo, nullptr, &descriptor_pool))
    }

    void create_descriptor_sets() {
        for (int eye = 0; eye < 2; eye++) {
            // OpenWarp descriptor sets
            std::cout << eye << std::endl;
            std::vector<VkDescriptorSetLayout> ow_layout = {buffer_pool->image_pool.size(), ow_descriptor_set_layout};
            VkDescriptorSetAllocateInfo        ow_alloc_info{
                VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, // sType
                nullptr,                                        // pNext
                {},                                             // descriptorPool
                0,                                              // descriptorSetCount
                nullptr                                         // pSetLayouts
            };
            ow_alloc_info.descriptorPool     = descriptor_pool;
            ow_alloc_info.descriptorSetCount = buffer_pool->image_pool.size();
            ow_alloc_info.pSetLayouts        = ow_layout.data();

            ow_descriptor_sets[eye].resize(buffer_pool->image_pool.size());
            VK_ASSERT_SUCCESS(vkAllocateDescriptorSets(ds->vk_device, &ow_alloc_info, ow_descriptor_sets[eye].data()))

            for (int image_idx = 0; image_idx < buffer_pool->image_pool.size(); image_idx++) {
                VkDescriptorImageInfo imageInfo = {};
                imageInfo.imageLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imageInfo.imageView             = buffer_pool->image_pool[image_idx][eye].image_view;
                imageInfo.sampler               = fb_sampler;

                // assert(buffer_pool[eye][0] != VK_NULL_HANDLE);

                VkDescriptorImageInfo depthInfo = {};
                depthInfo.imageLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                depthInfo.imageView             = buffer_pool->depth_image_pool[image_idx][eye].image_view;
                depthInfo.sampler               = fb_sampler;

                // assert(buffer_pool[eye][1] != VK_NULL_HANDLE);

                VkDescriptorBufferInfo bufferInfo = {};
                bufferInfo.buffer                 = ow_matrices_uniform_buffer;
                bufferInfo.offset                 = 0;
                bufferInfo.range                  = sizeof(WarpMatrices);

                std::array<VkWriteDescriptorSet, 3> owDescriptorWrites = {};

                owDescriptorWrites[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                owDescriptorWrites[0].dstSet          = ow_descriptor_sets[eye][image_idx];
                owDescriptorWrites[0].dstBinding      = 0;
                owDescriptorWrites[0].dstArrayElement = 0;
                owDescriptorWrites[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                owDescriptorWrites[0].descriptorCount = 1;
                owDescriptorWrites[0].pImageInfo      = &imageInfo;

                owDescriptorWrites[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                owDescriptorWrites[1].dstSet          = ow_descriptor_sets[eye][image_idx];
                owDescriptorWrites[1].dstBinding      = 1;
                owDescriptorWrites[1].dstArrayElement = 0;
                owDescriptorWrites[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                owDescriptorWrites[1].descriptorCount = 1;
                owDescriptorWrites[1].pImageInfo      = &depthInfo;

                owDescriptorWrites[2].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                owDescriptorWrites[2].dstSet          = ow_descriptor_sets[eye][image_idx];
                owDescriptorWrites[2].dstBinding      = 2;
                owDescriptorWrites[2].dstArrayElement = 0;
                owDescriptorWrites[2].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                owDescriptorWrites[2].descriptorCount = 1;
                owDescriptorWrites[2].pBufferInfo     = &bufferInfo;

                vkUpdateDescriptorSets(ds->vk_device, static_cast<uint32_t>(owDescriptorWrites.size()),
                                       owDescriptorWrites.data(), 0, nullptr);
            }

            // Distortion correction descriptor sets
            std::vector<VkDescriptorSetLayout> dc_layout     = {dc_descriptor_set_layout};
            VkDescriptorSetAllocateInfo        dc_alloc_info = {
                       VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, // sType
                       nullptr,                                        // pNext
                       {},                                             // descriptorPool
                       0,                                              // descriptorSetCount
                       nullptr                                         // pSetLayouts
            };
            dc_alloc_info.descriptorPool     = descriptor_pool;
            dc_alloc_info.descriptorSetCount = 1;
            dc_alloc_info.pSetLayouts        = dc_layout.data();

            dc_descriptor_sets[eye].resize(1);
            VK_ASSERT_SUCCESS(vkAllocateDescriptorSets(ds->vk_device, &dc_alloc_info, dc_descriptor_sets[eye].data()))

            VkDescriptorImageInfo offscreenImageInfo = {};
            offscreenImageInfo.imageLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            offscreenImageInfo.imageView             = offscreen_image_views[eye];
            offscreenImageInfo.sampler               = fb_sampler;

            std::array<VkWriteDescriptorSet, 1> dcDescriptorWrites = {};

            dcDescriptorWrites[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            dcDescriptorWrites[0].dstSet          = dc_descriptor_sets[eye][0];
            dcDescriptorWrites[0].dstBinding      = 0;
            dcDescriptorWrites[0].dstArrayElement = 0;
            dcDescriptorWrites[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            dcDescriptorWrites[0].descriptorCount = 1;
            dcDescriptorWrites[0].pImageInfo      = &offscreenImageInfo;

            vkUpdateDescriptorSets(ds->vk_device, static_cast<uint32_t>(dcDescriptorWrites.size()), dcDescriptorWrites.data(),
                                   0, nullptr);
        }
    }

    void create_openwarp_pipeline() {
        std::cout << "Creating openwarp renderpass" << std::endl;
        // A renderpass also has to be created
        VkAttachmentDescription color_attachment{};
        color_attachment.format         = VK_FORMAT_R8G8B8A8_UNORM; // this should match the offscreen image
        color_attachment.samples        = VK_SAMPLE_COUNT_1_BIT;
        color_attachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color_attachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        color_attachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color_attachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        color_attachment.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference color_attachment_ref{};
        color_attachment_ref.attachment = 0;
        color_attachment_ref.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentDescription depth_attachment{};
        depth_attachment.format         = VK_FORMAT_D16_UNORM; // this should match the offscreen image
        depth_attachment.samples        = VK_SAMPLE_COUNT_1_BIT;
        depth_attachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth_attachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        depth_attachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth_attachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        depth_attachment.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depth_attachment_ref{};
        depth_attachment_ref.attachment = 1;
        depth_attachment_ref.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount    = 1;
        subpass.pColorAttachments       = &color_attachment_ref;
        subpass.pDepthStencilAttachment = &depth_attachment_ref;

        std::array<VkAttachmentDescription, 2> all_attachments = {color_attachment, depth_attachment};

        VkSubpassDependency dependency{};
        dependency.srcSubpass    = 0;
        dependency.dstSubpass    = VK_SUBPASS_EXTERNAL;
        dependency.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependency.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        VkRenderPassCreateInfo render_pass_info{};
        render_pass_info.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        render_pass_info.attachmentCount = static_cast<uint32_t>(all_attachments.size());
        render_pass_info.pAttachments    = all_attachments.data();
        render_pass_info.subpassCount    = 1;
        render_pass_info.pSubpasses      = &subpass;
        render_pass_info.dependencyCount = 1;
        render_pass_info.pDependencies   = &dependency;

        VK_ASSERT_SUCCESS(vkCreateRenderPass(ds->vk_device, &render_pass_info, nullptr, &openwarp_render_pass));

        std::cout << "Openwarp renderpass created" << std::endl;

        if (openwarp_pipeline != VK_NULL_HANDLE) {
            throw std::runtime_error("openwarp_vk::create_pipeline: pipeline already created");
        }

        VkDevice device = ds->vk_device;

        auto           folder = std::string(SHADER_FOLDER);
        VkShaderModule vert   = vulkan::create_shader_module(device, vulkan::read_file(folder + "/openwarp_mesh.vert.spv"));
        VkShaderModule frag   = vulkan::create_shader_module(device, vulkan::read_file(folder + "/openwarp_mesh.frag.spv"));

        VkPipelineShaderStageCreateInfo vertStageInfo = {};
        vertStageInfo.sType                           = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertStageInfo.stage                           = VK_SHADER_STAGE_VERTEX_BIT;
        vertStageInfo.module                          = vert;
        vertStageInfo.pName                           = "main";

        VkPipelineShaderStageCreateInfo fragStageInfo = {};
        fragStageInfo.sType                           = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragStageInfo.stage                           = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragStageInfo.module                          = frag;
        fragStageInfo.pName                           = "main";

        VkPipelineShaderStageCreateInfo shaderStages[] = {vertStageInfo, fragStageInfo};

        auto bindingDescription    = OpenWarpVertex::get_binding_description();
        auto attributeDescriptions = OpenWarpVertex::get_attribute_descriptions();

        VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
        vertexInputInfo.sType                                = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount        = 1;
        vertexInputInfo.pVertexBindingDescriptions           = &bindingDescription;
        vertexInputInfo.vertexAttributeDescriptionCount      = static_cast<uint32_t>(attributeDescriptions.size());
        vertexInputInfo.pVertexAttributeDescriptions         = attributeDescriptions.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
        inputAssembly.sType                                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology                               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineRasterizationStateCreateInfo rasterizer = {};
        rasterizer.sType                                  = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode                            = VK_POLYGON_MODE_FILL;
        rasterizer.cullMode                               = VK_CULL_MODE_BACK_BIT;
        rasterizer.frontFace                              = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.lineWidth                              = 1.0f;
        rasterizer.depthClampEnable                       = VK_FALSE;
        rasterizer.rasterizerDiscardEnable                = VK_FALSE;
        rasterizer.depthBiasEnable                        = VK_FALSE;

        // disable multisampling
        VkPipelineMultisampleStateCreateInfo multisampling = {};
        multisampling.sType                                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples                 = VK_SAMPLE_COUNT_1_BIT;
        multisampling.sampleShadingEnable                  = VK_FALSE;

        VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
        colorBlendAttachment.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_FALSE;

        // disable blending
        VkPipelineColorBlendStateCreateInfo colorBlending = {};
        colorBlending.sType                               = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.attachmentCount                     = 1;
        colorBlending.pAttachments                        = &colorBlendAttachment;

        // enable depth testing
        VkPipelineDepthStencilStateCreateInfo depthStencil = {};
        depthStencil.sType                                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable                       = VK_TRUE;
        depthStencil.depthWriteEnable                      = VK_TRUE;
        depthStencil.minDepthBounds                        = 0.0f;
        depthStencil.maxDepthBounds                        = 1.0f;
        depthStencil.depthCompareOp =
            rendering_params::reverse_z ? VK_COMPARE_OP_GREATER_OR_EQUAL : VK_COMPARE_OP_LESS_OR_EQUAL;

        // use dynamic state instead of a fixed viewport
        std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

        VkPipelineDynamicStateCreateInfo dynamicStateCreateInfo = {};
        dynamicStateCreateInfo.sType                            = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicStateCreateInfo.dynamicStateCount                = static_cast<uint32_t>(dynamicStates.size());
        dynamicStateCreateInfo.pDynamicStates                   = dynamicStates.data();

        VkPipelineViewportStateCreateInfo viewportStateCreateInfo = {};
        viewportStateCreateInfo.sType                             = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportStateCreateInfo.viewportCount                     = 1;
        viewportStateCreateInfo.scissorCount                      = 1;

        VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
        pipelineLayoutInfo.sType                      = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount             = 1;
        pipelineLayoutInfo.pSetLayouts                = &ow_descriptor_set_layout;

        VkPushConstantRange push_constant = {};
        push_constant.stageFlags          = VK_SHADER_STAGE_VERTEX_BIT;
        push_constant.offset              = 0;
        push_constant.size                = sizeof(uint32_t);

        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges    = &push_constant;

        VK_ASSERT_SUCCESS(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &ow_pipeline_layout))

        VkGraphicsPipelineCreateInfo pipelineInfo = {};
        pipelineInfo.sType                        = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount                   = 2;
        pipelineInfo.pStages                      = shaderStages;
        pipelineInfo.pVertexInputState            = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState          = &inputAssembly;
        pipelineInfo.pViewportState               = &viewportStateCreateInfo;
        pipelineInfo.pRasterizationState          = &rasterizer;
        pipelineInfo.pMultisampleState            = &multisampling;
        pipelineInfo.pColorBlendState             = &colorBlending;
        pipelineInfo.pDepthStencilState           = &depthStencil;
        pipelineInfo.pDynamicState                = &dynamicStateCreateInfo;

        pipelineInfo.layout     = ow_pipeline_layout;
        pipelineInfo.renderPass = openwarp_render_pass;
        pipelineInfo.subpass    = 0;

        VK_ASSERT_SUCCESS(
            vkCreateGraphicsPipelines(ds->vk_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &openwarp_pipeline))

        vkDestroyShaderModule(device, vert, nullptr);
        vkDestroyShaderModule(device, frag, nullptr);
    }

    VkPipeline create_distortion_correction_pipeline(VkRenderPass render_pass, [[maybe_unused]] uint32_t subpass) {
        if (pipeline != VK_NULL_HANDLE) {
            throw std::runtime_error("openwarp_vk::create_distortion_correction_pipeline: pipeline already created");
        }

        VkDevice device = ds->vk_device;

        auto           folder = std::string(SHADER_FOLDER);
        VkShaderModule vert =
            vulkan::create_shader_module(device, vulkan::read_file(folder + "/distortion_correction.vert.spv"));
        VkShaderModule frag =
            vulkan::create_shader_module(device, vulkan::read_file(folder + "/distortion_correction.frag.spv"));

        VkPipelineShaderStageCreateInfo vertStageInfo = {};
        vertStageInfo.sType                           = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertStageInfo.stage                           = VK_SHADER_STAGE_VERTEX_BIT;
        vertStageInfo.module                          = vert;
        vertStageInfo.pName                           = "main";

        VkPipelineShaderStageCreateInfo fragStageInfo = {};
        fragStageInfo.sType                           = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragStageInfo.stage                           = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragStageInfo.module                          = frag;
        fragStageInfo.pName                           = "main";

        VkPipelineShaderStageCreateInfo shaderStages[] = {vertStageInfo, fragStageInfo};

        auto bindingDescription    = DistortionCorrectionVertex::get_binding_description();
        auto attributeDescriptions = DistortionCorrectionVertex::get_attribute_descriptions();

        VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
        vertexInputInfo.sType                                = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount        = 1;
        vertexInputInfo.pVertexBindingDescriptions           = &bindingDescription;
        vertexInputInfo.vertexAttributeDescriptionCount      = static_cast<uint32_t>(attributeDescriptions.size());
        vertexInputInfo.pVertexAttributeDescriptions         = attributeDescriptions.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
        inputAssembly.sType                                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology                               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineRasterizationStateCreateInfo rasterizer = {};
        rasterizer.sType                                  = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode                            = VK_POLYGON_MODE_FILL;
        rasterizer.cullMode                               = VK_CULL_MODE_NONE;
        rasterizer.frontFace                              = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.lineWidth                              = 1.0f;
        rasterizer.depthClampEnable                       = VK_FALSE;
        rasterizer.rasterizerDiscardEnable                = VK_FALSE;
        rasterizer.depthBiasEnable                        = VK_FALSE;

        // disable multisampling
        VkPipelineMultisampleStateCreateInfo multisampling = {};
        multisampling.sType                                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples                 = VK_SAMPLE_COUNT_1_BIT;
        multisampling.sampleShadingEnable                  = VK_FALSE;

        VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
        colorBlendAttachment.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_FALSE;

        // disable blending
        VkPipelineColorBlendStateCreateInfo colorBlending = {};
        colorBlending.sType                               = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.attachmentCount                     = 1;
        colorBlending.pAttachments                        = &colorBlendAttachment;

        // disable depth testing
        VkPipelineDepthStencilStateCreateInfo depthStencil = {};
        depthStencil.sType                                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable                       = VK_FALSE;

        // use dynamic state instead of a fixed viewport
        std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

        VkPipelineDynamicStateCreateInfo dynamicStateCreateInfo = {};
        dynamicStateCreateInfo.sType                            = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicStateCreateInfo.dynamicStateCount                = static_cast<uint32_t>(dynamicStates.size());
        dynamicStateCreateInfo.pDynamicStates                   = dynamicStates.data();

        VkPipelineViewportStateCreateInfo viewportStateCreateInfo = {};
        viewportStateCreateInfo.sType                             = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportStateCreateInfo.viewportCount                     = 1;
        viewportStateCreateInfo.scissorCount                      = 1;

        VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
        pipelineLayoutInfo.sType                      = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount             = 1;
        pipelineLayoutInfo.pSetLayouts                = &dc_descriptor_set_layout;

        VK_ASSERT_SUCCESS(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &dc_pipeline_layout))

        VkGraphicsPipelineCreateInfo pipelineInfo = {};
        pipelineInfo.sType                        = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount                   = 2;
        pipelineInfo.pStages                      = shaderStages;
        pipelineInfo.pVertexInputState            = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState          = &inputAssembly;
        pipelineInfo.pViewportState               = &viewportStateCreateInfo;
        pipelineInfo.pRasterizationState          = &rasterizer;
        pipelineInfo.pMultisampleState            = &multisampling;
        pipelineInfo.pColorBlendState             = &colorBlending;
        pipelineInfo.pDepthStencilState           = nullptr;
        pipelineInfo.pDynamicState                = &dynamicStateCreateInfo;

        pipelineInfo.layout     = dc_pipeline_layout;
        pipelineInfo.renderPass = render_pass;
        pipelineInfo.subpass    = 0;

        VK_ASSERT_SUCCESS(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline))

        vkDestroyShaderModule(device, vert, nullptr);
        vkDestroyShaderModule(device, frag, nullptr);
        return pipeline;
    }

    /* Compute a view matrix with rotation and position */
    static Eigen::Matrix4f create_camera_matrix(const pose_type& pose, int eye) {
        Eigen::Matrix4f cameraMatrix   = Eigen::Matrix4f::Identity();
        auto            ipd            = display_params::ipd / 2.0f;
        cameraMatrix.block<3, 1>(0, 3) = pose.position + pose.orientation * Eigen::Vector3f(eye == 0 ? -ipd : ipd, 0, 0);
        cameraMatrix.block<3, 3>(0, 0) = pose.orientation.toRotationMatrix();
        return cameraMatrix;
    }

    static Eigen::Matrix4f calculate_distortion_transform(const Eigen::Matrix4f& projection_matrix) {
        // Eigen stores matrices internally in column-major order.
        // However, the (i,j) accessors are row-major (i.e, the first argument
        // is which row, and the second argument is which column.)
        Eigen::Matrix4f texCoordProjection;
        texCoordProjection << 0.5f * projection_matrix(0, 0), 0.0f, 0.5f * projection_matrix(0, 2) - 0.5f, 0.0f, 0.0f,
            -0.5f * projection_matrix(1, 1), 0.5f * projection_matrix(1, 2) - 0.5f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f;

        return texCoordProjection;
    }

    const phonebook* const                    pb;
    const std::shared_ptr<switchboard>        sb;
    const std::shared_ptr<pose_prediction>    pp;
    bool                                      disable_warp = false;
    std::shared_ptr<vulkan::display_provider> ds           = nullptr;
    std::mutex                                m_setup;

    bool initialized                      = false;
    bool input_texture_vulkan_coordinates = true;

    bool using_godot = false;

    bool                   compare_images = false;
    std::vector<pose_type> fixed_poses;
    uint64_t               frame_count = 0;

    // Vulkan resources
    std::stack<std::function<void()>> deletion_queue;
    VmaAllocator                      vma_allocator{};

    // Note that each frame occupies 2 elements in the buffer pool:
    // i for the image itself, and i + 1 for the depth image.
    size_t                                               swapchain_width;
    size_t                                               swapchain_height;
    std::shared_ptr<vulkan::buffer_pool<fast_pose_type>> buffer_pool;
    VkSampler                                            fb_sampler{};

    VkDescriptorPool descriptor_pool{};
    VkCommandPool    command_pool{};
    VkCommandBuffer  command_buffer{};

    // offscreen image used as an intermediate render target
    std::array<VkImage, 2>       offscreen_images{};
    std::array<VkImageView, 2>   offscreen_image_views{};
    std::array<VmaAllocation, 2> offscreen_image_allocs{};

    std::array<VkImage, 2>       offscreen_depths{};
    std::array<VkImageView, 2>   offscreen_depth_views{};
    std::array<VmaAllocation, 2> offscreen_depth_allocs{};

    std::array<VkFramebuffer, 2> offscreen_framebuffers{};

    // openwarp mesh
    VkPipelineLayout ow_pipeline_layout{};

    VkBuffer          ow_matrices_uniform_buffer{};
    VmaAllocation     ow_matrices_uniform_alloc{};
    VmaAllocationInfo ow_matrices_uniform_alloc_info{};

    VkDescriptorSetLayout                       ow_descriptor_set_layout{};
    std::array<std::vector<VkDescriptorSet>, 2> ow_descriptor_sets;

    uint32_t                    num_openwarp_vertices;
    uint32_t                    num_openwarp_indices;
    std::vector<OpenWarpVertex> openwarp_vertices;
    std::vector<uint32_t>       openwarp_indices;
    size_t                      openwarp_width  = 0;
    size_t                      openwarp_height = 0;

    VkBuffer ow_vertex_buffer{};
    VkBuffer ow_index_buffer{};

    VkRenderPass openwarp_render_pass;
    VkPipeline   openwarp_pipeline = VK_NULL_HANDLE;

    // distortion data
    HMD::hmd_info_t hmd_info{};
    Eigen::Matrix4f basicProjection[2];
    Eigen::Matrix4f invProjection[2];

    VkPipelineLayout  dc_pipeline_layout{};
    VkBuffer          dc_uniform_buffer{};
    VmaAllocation     dc_uniform_alloc{};
    VmaAllocationInfo dc_uniform_alloc_info{};

    VkDescriptorSetLayout                       dc_descriptor_set_layout{};
    std::array<std::vector<VkDescriptorSet>, 2> dc_descriptor_sets;

    uint32_t                                num_distortion_vertices{};
    uint32_t                                num_distortion_indices{};
    std::vector<DistortionCorrectionVertex> distortion_vertices;
    std::vector<uint32_t>                   distortion_indices;

    VkRenderPass distortion_correction_render_pass;
    VkBuffer     dc_vertex_buffer{};
    VkBuffer     dc_index_buffer{};

    // metrics
    std::atomic<uint32_t> num_record_calls{0};
    std::atomic<uint32_t> num_update_uniforms_calls{0};

    friend class openwarp_vk_plugin;
};

class openwarp_vk_plugin : public threadloop {
public:
    openwarp_vk_plugin(const std::string& name, phonebook* pb)
        : threadloop{name, pb}
        , tw{std::make_shared<openwarp_vk>(pb)} {
        pb->register_impl<vulkan::timewarp>(std::static_pointer_cast<vulkan::timewarp>(tw));
    }

    void _p_one_iteration() override {
        auto fps = tw->num_record_calls.exchange(0) / 2; // two eyes
        auto ups = tw->num_update_uniforms_calls.exchange(0);

        // std::cout << "openwarp_vk: cb records: " << fps << ", uniform updates: " << ups << std::endl;
    }

    skip_option _p_should_skip() override {
        // Get the current time in milliseconds
        auto now =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

        // Only print every 1 second
        if (now - last_print < 1000) {
            return skip_option::skip_and_yield;
        } else {
            last_print = now;
            return skip_option::run;
        }
    }

private:
    std::shared_ptr<openwarp_vk>              tw;
    std::shared_ptr<vulkan::display_provider> ds;

    int64_t last_print = 0;
};

PLUGIN_MAIN(openwarp_vk_plugin)
