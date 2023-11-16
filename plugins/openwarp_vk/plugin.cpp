#if defined(ILLIXR_MONADO)
    #define VMA_IMPLEMENTATION
#endif

#include "illixr/data_format.hpp"
#include "illixr/global_module_defs.hpp"
#include "illixr/math_util.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/pose_prediction.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"
#include "illixr/vk/display_provider.hpp"
#include "illixr/vk/render_pass.hpp"
#include "illixr/vk/vulkan_utils.hpp"
#include "utils/hmd.hpp"

#include <future>
#include <iostream>
#include <mutex>
#include <stack>
#include <vulkan/vulkan_core.h>

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
            offsetof(DistortionCorrectionVertex, pos); // number of bytes since the start of the per-vertex data to read from

        // uv
        attribute_descriptions[1].binding  = 0;
        attribute_descriptions[1].location = 1;
        attribute_descriptions[1].format   = VK_FORMAT_R32G32_SFLOAT;
        attribute_descriptions[1].offset   = offsetof(DistortionCorrectionVertex, uv0);

        return attribute_descriptions;
    }
};

struct WarpMatrices {
    glm::mat4 render_inv_projection;
    glm::mat4 render_inv_view;
    glm::mat4 warp_view_projection;
};

class openwarp_vk : public vulkan::timewarp {
public:
    explicit openwarp_vk(const phonebook* const pb)
        : pb{pb}
        , sb{pb->lookup_impl<switchboard>()}
        , pp{pb->lookup_impl<pose_prediction>()}
        , disable_warp{ILLIXR::str_to_bool(ILLIXR::getenv_or("ILLIXR_TIMEWARP_DISABLE", "False"))} { }

    void initialize() {
        ds = pb->lookup_impl<vulkan::display_provider>();

        if (ds->vma_allocator) {
            this->vma_allocator = ds->vma_allocator;
        } else {
            this->vma_allocator =
                vulkan::vulkan_utils::create_vma_allocator(ds->vk_instance, ds->vk_physical_device, ds->vk_device);
            deletion_queue.emplace([=]() {
                vmaDestroyAllocator(vma_allocator);
            });
        }

        HMD::GetDefaultHmdInfo(ds->swapchain_extent.width == 0 ? display_params::width_pixels : ds->swapchain_extent.width,
                               ds->swapchain_extent.height == 0 ? display_params::height_pixels : ds->swapchain_extent.height,
                               display_params::width_meters, display_params::height_meters, display_params::lens_separation,
                               display_params::meters_per_tan_angle, display_params::aberration, hmd_info);

        generate_openwarp_mesh(512, 512);
        generate_distortion_data();

        command_pool = vulkan::vulkan_utils::create_command_pool(
            ds->vk_device, ds->queues[vulkan::vulkan_utils::queue::queue_type::GRAPHICS].family);
        command_buffer = vulkan::vulkan_utils::create_command_buffer(ds->vk_device, command_pool);
        deletion_queue.emplace([=]() {
            vkDestroyCommandPool(ds->vk_device, command_pool, nullptr);
        });

        create_vertex_buffers();
        create_index_buffers();

        create_descriptor_set_layouts();
        create_uniform_buffer();
        create_texture_sampler();
    }

    void setup(VkRenderPass render_pass, uint32_t subpass, std::array<std::vector<VkImageView>, 2> buffer_pool_in,
               bool input_texture_vulkan_coordinates_in) override {
        std::lock_guard<std::mutex> lock{m_setup};

        this->input_texture_vulkan_coordinates = input_texture_vulkan_coordinates_in;
        if (!initialized) {
            initialize();
            initialized = true;
        } else {
            partial_destroy();
        }

        if (buffer_pool_in[0].size() != buffer_pool_in[1].size()) {
            throw std::runtime_error("timewarp_vk: buffer_pool[0].size() != buffer_pool[1].size()");
        }

        this->buffer_pool = buffer_pool_in;

        create_descriptor_pool();
        create_descriptor_sets();
        create_openwarp_pipeline();
        create_distortion_correction_pipeline(render_pass, subpass);
    }

    void partial_destroy() {
        vkDestroyPipeline(ds->vk_device, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;

        vkDestroyPipelineLayout(ds->vk_device, dc_pipeline_layout, nullptr);
        dc_pipeline_layout = VK_NULL_HANDLE;

        vkDestroyDescriptorPool(ds->vk_device, descriptor_pool, nullptr);
        descriptor_pool = VK_NULL_HANDLE;
    }

    void update_uniforms(const pose_type& render_pose) override {
        num_update_uniforms_calls++;

        const pose_type latest_pose       = disable_warp ? render_pose : pp->get_fast_pose().pose;
        Eigen::Matrix4f viewMatrix = create_camera_matrix(latest_pose);
        Eigen::Matrix4f invView = viewMatrix.inverse();
        Eigen::Matrix4f warpVP = basicProjection * invView;

        auto* ubo = (WarpMatrices*) ow_matrices_uniform_alloc_info.pMappedData;
        memcpy(&ubo->render_inv_projection, invProjection.data(), sizeof(glm::mat4));
        memcpy(&ubo->render_inv_view, invView.data(), sizeof(glm::mat4));
        memcpy(&ubo->warp_view_projection, warpVP.data(), sizeof(glm::mat4));
    }

    void record_command_buffer(VkCommandBuffer commandBuffer, int buffer_ind, bool left) override {
        num_record_calls++;

        // First perform openwarp on a mesh
        // to-do: Monado will begin the render pass; how to workaround?

        // to-do: need some synchronization here

        // Then perform distortion correction
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &dc_vertex_buffer, offsets);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, dc_pipeline_layout, 0, 1,
                                &dc_descriptor_sets[!left][buffer_ind], 0, nullptr);
        vkCmdBindIndexBuffer(commandBuffer, dc_index_buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(commandBuffer, num_distortion_indices, 1, 0, static_cast<int>(num_distortion_vertices * !left), 0);
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
    void create_offscreen_image(size_t width, size_t height) {
        VkImageCreateInfo image_info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        image_info.imageType = VK_IMAGE_TYPE_2D;
        image_info.extent.width = width;
        image_info.extent.height = height;
        image_info.extent.depth = 1;
        image_info.mipLevels = 1;
        image_info.arrayLayers = 1;
        image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
        image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;
        
        VmaAllocationCreateInfo create_info = {};
        create_info.usage = VMA_MEMORY_USAGE_AUTO;
        create_info.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
        create_info.priority = 1.0f;
        
        VK_ASSERT_SUCCESS(vmaCreateImage(ds->vma_allocator, &image_info, &create_info, &offscreen_image, &offscreen_alloc, nullptr));

        VkImageViewCreateInfo view_info = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view_info.image = offscreen_image;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
        view_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.baseMipLevel = 0;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount = 1;
    
        VK_ASSERT_SUCCESS(vkCreateImageView(ds->vk_device, &view_info, nullptr, &offscreen_image_view));

        // Need a framebuffer to render to
        VkFramebufferCreateInfo framebuffer_info = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = openwarp_render_pass,
            .attachmentCount = 1,
            .pAttachments = &offscreen_image_view,
            .width = width,
            .height = height,
            .layers = 1,
        };

        VK_ASSERT_SUCCESS(vkCreateFramebuffer(ds->vk_device, &framebuffer_info, nullptr, &openwarp_fb));
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
        memcpy(ow_mapped_data, distortion_vertices.data(), sizeof(OpenWarpVertex) * num_openwarp_vertices);
        vmaUnmapMemory(vma_allocator, ow_staging_alloc);

        VkCommandBuffer ow_command_buffer_local = vulkan::vulkan_utils::begin_one_time_command(ds->vk_device, command_pool);
        VkBufferCopy    ow_copy_region          = {};
        ow_copy_region.size                     = sizeof(OpenWarpVertex) * num_openwarp_vertices;
        vkCmdCopyBuffer(ow_command_buffer_local, ow_staging_buffer, ow_vertex_buffer, 1, &ow_copy_region);
        vulkan::vulkan_utils::end_one_time_command(ds->vk_device, command_pool,
                                                   ds->queues[vulkan::vulkan_utils::queue::queue_type::GRAPHICS].vk_queue,
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
        memcpy(dc_mapped_data, distortion_vertices.data(), sizeof(DistortionCorrectionVertex) * num_distortion_vertices * HMD::NUM_EYES);
        vmaUnmapMemory(vma_allocator, dc_staging_alloc);

        VkCommandBuffer dc_command_buffer_local = vulkan::vulkan_utils::begin_one_time_command(ds->vk_device, command_pool);
        VkBufferCopy    dc_copy_region          = {};
        dc_copy_region.size                     = sizeof(DistortionCorrectionVertex) * num_distortion_vertices * HMD::NUM_EYES;
        vkCmdCopyBuffer(dc_command_buffer_local, dc_staging_buffer, dc_vertex_buffer, 1, &dc_copy_region);
        vulkan::vulkan_utils::end_one_time_command(ds->vk_device, command_pool,
                                                   ds->queues[vulkan::vulkan_utils::queue::queue_type::GRAPHICS].vk_queue,
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
        ow_staging_buffer_info.size  = sizeof(uint32_t) * num_distortion_indices;
        ow_staging_buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

        VmaAllocationCreateInfo ow_staging_alloc_info = {};
        ow_staging_alloc_info.usage                   = VMA_MEMORY_USAGE_AUTO;
        ow_staging_alloc_info.flags                   = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

        VkBuffer      ow_staging_buffer;
        VmaAllocation ow_staging_alloc;
        VK_ASSERT_SUCCESS(
            vmaCreateBuffer(vma_allocator, &ow_staging_buffer_info, &ow_staging_alloc_info, &ow_staging_buffer, &ow_staging_alloc, nullptr))

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
        ow_buffer_info.size  = sizeof(uint32_t) * num_distortion_indices;
        ow_buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

        VmaAllocationCreateInfo ow_alloc_info = {};
        ow_alloc_info.usage                   = VMA_MEMORY_USAGE_GPU_ONLY;

        VmaAllocation ow_index_alloc;
        VK_ASSERT_SUCCESS(vmaCreateBuffer(vma_allocator, &ow_buffer_info, &ow_alloc_info, &ow_index_buffer, &ow_index_alloc, nullptr))

        void* ow_mapped_data;
        VK_ASSERT_SUCCESS(vmaMapMemory(vma_allocator, ow_staging_alloc, &ow_mapped_data))
        memcpy(ow_mapped_data, openwarp_indices.data(), sizeof(uint32_t) * num_openwarp_indices);
        vmaUnmapMemory(vma_allocator, ow_staging_alloc);

        VkCommandBuffer ow_command_buffer_local = vulkan::vulkan_utils::begin_one_time_command(ds->vk_device, command_pool);
        VkBufferCopy    ow_copy_region          = {};
        ow_copy_region.size                     = sizeof(uint32_t) * num_openwarp_indices;
        vkCmdCopyBuffer(ow_command_buffer_local, ow_staging_buffer, ow_index_buffer, 1, &ow_copy_region);
        vulkan::vulkan_utils::end_one_time_command(ds->vk_device, command_pool,
                                                   ds->queues[vulkan::vulkan_utils::queue::queue_type::GRAPHICS].vk_queue,
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
        VK_ASSERT_SUCCESS(
            vmaCreateBuffer(vma_allocator, &dc_staging_buffer_info, &dc_staging_alloc_info, &dc_staging_buffer, &dc_staging_alloc, nullptr))

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
        VK_ASSERT_SUCCESS(vmaCreateBuffer(vma_allocator, &dc_buffer_info, &dc_alloc_info, &dc_index_buffer, &dc_index_alloc, nullptr))

        void* dc_mapped_data;
        VK_ASSERT_SUCCESS(vmaMapMemory(vma_allocator, dc_staging_alloc, &dc_mapped_data))
        memcpy(dc_mapped_data, distortion_indices.data(), sizeof(uint32_t) * num_distortion_indices);
        vmaUnmapMemory(vma_allocator, dc_staging_alloc);

        VkCommandBuffer dc_command_buffer_local = vulkan::vulkan_utils::begin_one_time_command(ds->vk_device, command_pool);
        VkBufferCopy    dc_copy_region          = {};
        dc_copy_region.size                     = sizeof(uint32_t) * num_distortion_indices;
        vkCmdCopyBuffer(dc_command_buffer_local, dc_staging_buffer, dc_index_buffer, 1, &dc_copy_region);
        vulkan::vulkan_utils::end_one_time_command(ds->vk_device, command_pool,
                                                   ds->queues[vulkan::vulkan_utils::queue::queue_type::GRAPHICS].vk_queue,
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

        for (int eye = 0; eye < HMD::NUM_EYES; eye++) {
            for (int y = 0; y <= hmd_info.eyeTilesHigh; y++) {
                for (int x = 0; x <= hmd_info.eyeTilesWide; x++) {
                    const int index = y * (hmd_info.eyeTilesWide + 1) + x;

                    // Set the physical distortion mesh coordinates. These are rectangular/gridlike, not distorted.
                    // The distortion is handled by the UVs, not the actual mesh coordinates!
                    // distortion_positions[eye * num_distortion_vertices + index].x = (-1.0f + eye + ((float) x /
                    // hmd_info.eyeTilesWide));
                    distortion_vertices[eye * num_distortion_vertices + index].pos.x =
                        (-1.0f + (static_cast<float>(x) / static_cast<float>(hmd_info.eyeTilesWide)));

                    // flip the y coordinates for Vulkan texture
                    distortion_vertices[eye * num_distortion_vertices + index].pos.y =
                        (input_texture_vulkan_coordinates ? -1.0f : 1.0f) *
                        (-1.0f +
                         2.0f * (static_cast<float>(hmd_info.eyeTilesHigh - y) / static_cast<float>(hmd_info.eyeTilesHigh)) *
                             (static_cast<float>(hmd_info.eyeTilesHigh * hmd_info.tilePixelsHigh) /
                              static_cast<float>(hmd_info.displayPixelsHigh)));
                    distortion_vertices[eye * num_distortion_vertices + index].pos.z = 0.0f;

                    // Use the previously-calculated distort_coords to set the UVs on the distortion mesh
                    distortion_vertices[eye * num_distortion_vertices + index].uv0.x = distort_coords[eye][0][index].x;
                    distortion_vertices[eye * num_distortion_vertices + index].uv0.y = distort_coords[eye][0][index].y;
                    distortion_vertices[eye * num_distortion_vertices + index].uv1.x = distort_coords[eye][1][index].x;
                    distortion_vertices[eye * num_distortion_vertices + index].uv1.y = distort_coords[eye][1][index].y;
                    distortion_vertices[eye * num_distortion_vertices + index].uv2.x = distort_coords[eye][2][index].x;
                    distortion_vertices[eye * num_distortion_vertices + index].uv2.y = distort_coords[eye][2][index].y;
                }
            }
        }

        // Construct perspective projection matrix
        math_util::projection_fov(&basicProjection, display_params::fov_x / 2.0f, display_params::fov_x / 2.0f,
                                  display_params::fov_y / 2.0f, display_params::fov_y / 2.0f, rendering_params::near_z,
                                  rendering_params::far_z);
        invProjection = basicProjection.inverse();
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

                openwarp_vertices[index].pos.x = ((float) x / width);
                openwarp_vertices[index].pos.y = (((height - (float) y) / height));

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
        depthLayoutBinding.stageFlags                   = VK_SHADER_STAGE_VERTEX_BIT;

        VkDescriptorSetLayoutBinding matrixUboLayoutBinding = {};
        matrixUboLayoutBinding.binding                      = 2;
        matrixUboLayoutBinding.descriptorType               = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        matrixUboLayoutBinding.descriptorCount              = 1;
        matrixUboLayoutBinding.stageFlags                   = VK_SHADER_STAGE_VERTEX_BIT;

        std::array<VkDescriptorSetLayoutBinding, 3> ow_bindings   = {imageLayoutBinding, depthLayoutBinding, matrixUboLayoutBinding};
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


        std::array<VkDescriptorSetLayoutBinding, 1> dc_bindings   = {offscreenImageLayoutBinding};
        VkDescriptorSetLayoutCreateInfo             dc_layout_info = {};
        dc_layout_info.sType                                       = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dc_layout_info.bindingCount                                = static_cast<uint32_t>(dc_bindings.size());
        dc_layout_info.pBindings = dc_bindings.data(); // array of VkDescriptorSetLayoutBinding structs

        VK_ASSERT_SUCCESS(vkCreateDescriptorSetLayout(ds->vk_device, &dc_layout_info, nullptr, &dc_descriptor_set_layout))
        deletion_queue.emplace([=]() {
            vkDestroyDescriptorSetLayout(ds->vk_device, dc_descriptor_set_layout, nullptr);
        });
    }

    void create_uniform_buffer() {
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

        VK_ASSERT_SUCCESS(
            vmaCreateBuffer(vma_allocator, &matrixBufferInfo, &createInfo, &ow_matrices_uniform_buffer, &ow_matrices_uniform_alloc, &ow_matrices_uniform_alloc_info))
        deletion_queue.emplace([=]() {
            vmaDestroyBuffer(vma_allocator, ow_matrices_uniform_buffer, ow_matrices_uniform_alloc);
        });
    }

    void create_descriptor_pool() {
        std::array<VkDescriptorPoolSize, 2> poolSizes = {};
        poolSizes[0].type                             = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSizes[0].descriptorCount                  = 2;
        poolSizes[1].type                             = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSizes[1].descriptorCount                  = 4;

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
        poolInfo.maxSets       = buffer_pool[0].size() * 2;

        VK_ASSERT_SUCCESS(vkCreateDescriptorPool(ds->vk_device, &poolInfo, nullptr, &descriptor_pool))
    }

    void create_descriptor_sets() {
        // single frame in flight for now
        for (int eye = 0; eye < 2; eye++) {
            // OpenWarp descriptor sets
            std::vector<VkDescriptorSetLayout> ow_layout = {ow_descriptor_set_layout};
            VkDescriptorSetAllocateInfo ow_alloc_info {
                VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, // sType
                nullptr,                                        // pNext
                {},                                             // descriptorPool
                0,                                              // descriptorSetCount
                nullptr                                         // pSetLayouts
            };
            ow_alloc_info.descriptorPool     = descriptor_pool;
            ow_alloc_info.descriptorSetCount = 1;
            ow_alloc_info.pSetLayouts        = ow_layout.data();

            ow_descriptor_sets[eye].resize(1);
            VK_ASSERT_SUCCESS(vkAllocateDescriptorSets(ds->vk_device, &ow_alloc_info, ow_descriptor_sets[eye].data()))

            VkDescriptorImageInfo imageInfo = {};
            imageInfo.imageLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfo.imageView             = buffer_pool[eye][0];
            imageInfo.sampler               = fb_sampler;

            assert(buffer_pool[eye][0] != VK_NULL_HANDLE);

            VkDescriptorImageInfo depthInfo = {};
            imageInfo.imageLayout           = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            imageInfo.imageView             = buffer_pool[eye][1];
            imageInfo.sampler               = fb_sampler;

            assert(buffer_pool[eye][1] != VK_NULL_HANDLE);

            VkDescriptorBufferInfo bufferInfo = {};
            bufferInfo.buffer                 = dc_uniform_buffer;
            bufferInfo.offset                 = 0;
            bufferInfo.range                  = sizeof(WarpMatrices);

            std::array<VkWriteDescriptorSet, 3> owDescriptorWrites = {};

            owDescriptorWrites[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            owDescriptorWrites[0].dstSet          = ow_descriptor_sets[eye][0];
            owDescriptorWrites[0].dstBinding      = 0;
            owDescriptorWrites[0].dstArrayElement = 0;
            owDescriptorWrites[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            owDescriptorWrites[0].descriptorCount = 1;
            owDescriptorWrites[0].pImageInfo      = &imageInfo;

            owDescriptorWrites[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            owDescriptorWrites[1].dstSet          = ow_descriptor_sets[eye][0];
            owDescriptorWrites[1].dstBinding      = 1;
            owDescriptorWrites[1].dstArrayElement = 0;
            owDescriptorWrites[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            owDescriptorWrites[1].descriptorCount = 1;
            owDescriptorWrites[1].pImageInfo      = &depthInfo;

            owDescriptorWrites[2].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            owDescriptorWrites[2].dstSet          = ow_descriptor_sets[eye][0];
            owDescriptorWrites[2].dstBinding      = 2;
            owDescriptorWrites[2].dstArrayElement = 0;
            owDescriptorWrites[2].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            owDescriptorWrites[2].descriptorCount = 1;
            owDescriptorWrites[2].pBufferInfo     = &bufferInfo;


            vkUpdateDescriptorSets(ds->vk_device, static_cast<uint32_t>(owDescriptorWrites.size()), owDescriptorWrites.data(),
                                    0, nullptr);


            // Distortion correction descriptor sets

            std::vector<VkDescriptorSetLayout> dc_layout   = {dc_descriptor_set_layout};
            VkDescriptorSetAllocateInfo        dc_alloc_info = {
                VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, // sType
                nullptr,                                        // pNext
                {},                                             // descriptorPool
                0,                                              // descriptorSetCount
                nullptr                                         // pSetLayouts
            };
            dc_alloc_info.descriptorPool     = descriptor_pool;
            dc_alloc_info.descriptorSetCount = 1; // to-do: find a better way to keep track of number of descriptor sets
            dc_alloc_info.pSetLayouts        = dc_layout.data();

            dc_descriptor_sets[eye].resize(1);
            VK_ASSERT_SUCCESS(vkAllocateDescriptorSets(ds->vk_device, &dc_alloc_info, dc_descriptor_sets[eye].data()))

            VkDescriptorImageInfo offscreenImageInfo = {};
            offscreenImageInfo.imageLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            offscreenImageInfo.imageView             = offscreen_image_view;
            offscreenImageInfo.sampler               = fb_sampler;

            std::array<VkWriteDescriptorSet, 1> dcDescriptorWrites = {};

            dcDescriptorWrites[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            dcDescriptorWrites[0].dstSet          = dc_descriptor_sets[eye][0];
            dcDescriptorWrites[0].dstBinding      = 0;
            dcDescriptorWrites[0].dstArrayElement = 0;
            dcDescriptorWrites[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            dcDescriptorWrites[0].descriptorCount = 1;
            dcDescriptorWrites[0].pImageInfo     = &offscreenImageInfo;

            vkUpdateDescriptorSets(ds->vk_device, static_cast<uint32_t>(dcDescriptorWrites.size()), dcDescriptorWrites.data(),
                                    0, nullptr);
        }
    }

    void create_openwarp_pipeline() {
        // A renderpass also has to be created
        VkAttachmentDescription color_attachment{};
        color_attachment.format = VK_FORMAT_R8G8B8A8_UNORM; // this should match the offscreen image
        color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        color_attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference color_attachment_ref{};
        color_attachment_ref.attachment = 0;
        color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color_attachment_ref;

        VkRenderPassCreateInfo render_pass_info{};
        render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        render_pass_info.attachmentCount = 1;
        render_pass_info.pAttachments = &color_attachment;
        render_pass_info.subpassCount = 1;
        render_pass_info.pSubpasses = &subpass;
        render_pass_info.dependencyCount = 0;
        render_pass_info.pDependencies = nullptr;

        VK_ASSERT_SUCCESS(vkCreateRenderPass(ds->vk_device, &render_pass_info, nullptr, &openwarp_render_pass));

        if (openwarp_pipeline != VK_NULL_HANDLE) {
            throw std::runtime_error("openwarp_vk::create_pipeline: pipeline already created");
        }

        VkDevice device = ds->vk_device;

        auto           folder = std::string(SHADER_FOLDER);
        VkShaderModule vert   = vulkan::vulkan_utils::create_shader_module(
            device, vulkan::vulkan_utils::read_file(folder + "/openwarp_mesh.vert.spv"));
        VkShaderModule frag = vulkan::vulkan_utils::create_shader_module(
            device, vulkan::vulkan_utils::read_file(folder + "/openwarp_mesh.frag.spv"));

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
        pipelineLayoutInfo.pSetLayouts                = &ow_descriptor_set_layout;

        std::cout << "Creating pipeline layout" << std::endl;
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
        pipelineInfo.pDepthStencilState           = nullptr;
        pipelineInfo.pDynamicState                = &dynamicStateCreateInfo;

        pipelineInfo.layout     = ow_pipeline_layout;
        pipelineInfo.renderPass = openwarp_render_pass;
        pipelineInfo.subpass    = 0;

        std::cout << "Creating graphics pipeline" << std::endl;
        VK_ASSERT_SUCCESS(vkCreateGraphicsPipelines(ds->vk_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &openwarp_pipeline))

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
            vulkan::vulkan_utils::create_shader_module(device, vulkan::vulkan_utils::read_file(folder + "/distortion_correction.vert.spv"));
        VkShaderModule frag =
            vulkan::vulkan_utils::create_shader_module(device, vulkan::vulkan_utils::read_file(folder + "/distortion_correction.frag.spv"));

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

        std::cout << "Creating pipeline layout" << std::endl;
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

        std::cout << "Creating graphics pipeline" << std::endl;
        VK_ASSERT_SUCCESS(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline))

        vkDestroyShaderModule(device, vert, nullptr);
        vkDestroyShaderModule(device, frag, nullptr);
        return pipeline;
    }

    /* Compute a view matrix with rotation and position */
    static Eigen::Matrix4f create_camera_matrix(const pose_type& pose) {
        Eigen::Matrix4f cameraMatrix = Eigen::Matrix4f::Identity();
        cameraMatrix.block<3,1>(0,3) = pose.position;
        cameraMatrix.block<3,3>(0,0) = pose.orientation.toRotationMatrix();
        return cameraMatrix;
    }

    const phonebook* const                    pb;
    const std::shared_ptr<switchboard>        sb;
    const std::shared_ptr<pose_prediction>    pp;
    bool                                      disable_warp = false;
    std::shared_ptr<vulkan::display_provider> ds           = nullptr;
    std::mutex                                m_setup;

    bool initialized                      = false;
    bool input_texture_vulkan_coordinates = true;

    // Vulkan resources
    std::stack<std::function<void()>> deletion_queue;
    VmaAllocator                      vma_allocator{};

    // Note that each frame occupies 2 elements in the buffer pool:
    // i for the image itself, and i + 1 for the depth image.
    std::array<std::vector<VkImageView>, 2> buffer_pool;
    VkSampler                               fb_sampler{};

    VkDescriptorPool descriptor_pool{};
    VkCommandPool    command_pool{};
    VkCommandBuffer  command_buffer{};

    // offscreen image used as an intermediate render target
    VkImage offscreen_image;
    VkImageView offscreen_image_view;
    VmaAllocation offscreen_alloc{};

    // openwarp mesh
    VkPipelineLayout  ow_pipeline_layout{};

    VkBuffer          ow_matrices_uniform_buffer{};
    VmaAllocation     ow_matrices_uniform_alloc{};
    VmaAllocationInfo ow_matrices_uniform_alloc_info{};

    VkBuffer          ow_params_uniform_buffer{};
    VmaAllocation     ow_params_uniform_alloc{};
    VmaAllocationInfo ow_params_uniform_alloc_info{};
    
    VkDescriptorSetLayout                       ow_descriptor_set_layout{};
    std::array<std::vector<VkDescriptorSet>, 2> ow_descriptor_sets;

    uint32_t                         num_openwarp_vertices;
    uint32_t                         num_openwarp_indices;
    std::vector<OpenWarpVertex>      openwarp_vertices;
    std::vector<uint32_t>            openwarp_indices;

    VkBuffer ow_vertex_buffer{};
    VkBuffer ow_index_buffer{};

    VkRenderPass openwarp_render_pass;
    VkPipeline   openwarp_pipeline;
    VkFramebuffer openwarp_fb;

    // distortion data
    HMD::hmd_info_t hmd_info{};
    Eigen::Matrix4f basicProjection;
    Eigen::Matrix4f invProjection;

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

    VkBuffer dc_vertex_buffer{};
    VkBuffer dc_index_buffer{};

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