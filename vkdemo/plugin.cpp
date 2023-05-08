#define VMA_IMPLEMENTATION
#include "common/data_format.hpp"
#include "common/error_util.hpp"
#include "common/global_module_defs.hpp"
#include "common/math_util.hpp"
#include "common/pose_prediction.hpp"
#include "common/switchboard.hpp"
#include "common/threadloop.hpp"
#include "common/vk_util/display_sink.hpp"
#include "common/vk_util/render_pass.hpp"

#define TINYOBJLOADER_IMPLEMENTATION
#include "common/gl_util/lib/tiny_obj_loader.h"

#include <array>
#include <chrono>
#include <cmath>
#include <eigen3/Eigen/src/Core/Matrix.h>
#include <future>
#include <glm/detail/qualifier.hpp>
#include <glm/fwd.hpp>
#include <iostream>
#include <thread>
#include "unordered_map"
#include <vulkan/vulkan_core.h>

using namespace ILLIXR;

#ifndef NDEBUG
#define SHADER_FOLDER "vkdemo/build/Debug/shaders"
#else
#define SHADER_FOLDER "vkdemo/build/Release/shaders"
#endif

struct Vertex {
    glm::vec3 pos;
    glm::vec2 uv;

    static VkVertexInputBindingDescription get_binding_description() {
        VkVertexInputBindingDescription binding_description{};
        binding_description.binding = 0;
        binding_description.stride = sizeof(Vertex);
        binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        return binding_description;
    }

    static std::array<VkVertexInputAttributeDescription, 2> get_attribute_descriptions() {
        std::array<VkVertexInputAttributeDescription, 2> attribute_descriptions{};
        attribute_descriptions[0].binding = 0;
        attribute_descriptions[0].location = 0;
        attribute_descriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attribute_descriptions[0].offset = offsetof(Vertex, pos);
        attribute_descriptions[1].binding = 0;
        attribute_descriptions[1].location = 1;
        attribute_descriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
        attribute_descriptions[1].offset = offsetof(Vertex, uv);

        return attribute_descriptions;
    }

    bool operator==(const Vertex& other) const {
        return pos == other.pos && uv == other.uv;
    }
};

namespace std {
    template<> struct hash<Vertex> {
        size_t operator()(Vertex const& vertex) const {
            return ((hash<glm::vec3>()(vertex.pos) ^ (hash<glm::vec2>()(vertex.uv) << 1)) >> 1);
        }
    };
}

struct UniformBufferObject {
    glm::mat4 model_view;
    glm::mat4 proj;
};

class vkdemo : public app {
public:
    vkdemo(const phonebook* const pb)
        : sb{pb->lookup_impl<switchboard>()}
        , ds{pb->lookup_impl<display_sink>()}
        , _m_clock{pb->lookup_impl<RelativeClock>()}
        , _m_vsync{sb->get_reader<switchboard::event_wrapper<time_point>>("vsync_estimate")} { }

    void initialize() {
        if (ds->vma_allocator) {
            this->vma_allocator = ds->vma_allocator;
        } else {
            this->vma_allocator = vulkan_utils::create_vma_allocator(ds->vk_instance, ds->vk_physical_device, ds->vk_device);
        }

        load_model();
        command_pool = vulkan_utils::create_command_pool(ds->vk_device, ds->graphics_queue_family);
        command_buffer = vulkan_utils::create_command_buffer(ds->vk_device, command_pool);
        create_descriptor_set_layout();
        create_uniform_buffers();
        create_descriptor_pool();
        create_descriptor_set();
        create_vertex_buffer();
        create_index_buffer();

        // Construct perspective projection matrix
        math_util::projection_fov(&basic_projection, display_params::fov_x / 2.0f, display_params::fov_x / 2.0f,
                                  display_params::fov_y / 2.0f, display_params::fov_y / 2.0f, rendering_params::near_z,
                                  rendering_params::far_z);
    }

    virtual void setup(VkRenderPass render_pass, uint32_t subpass) override {
        create_pipeline(render_pass, subpass);
    }

    virtual void update_uniforms(const fast_pose_type fp) override {
        update_uniform(fp, 0);
        update_uniform(fp, 1);
    }

    virtual void record_command_buffer(VkCommandBuffer commandBuffer, int eye) override {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        VkBuffer vertexBuffers[] = {vertex_buffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(commandBuffer, index_buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &descriptor_sets[eye], 0,
                                nullptr);
        vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);
    }

private:

    void update_uniform(const fast_pose_type& fp, int eye) {
        Eigen::Matrix4f modelMatrix = Eigen::Matrix4f::Identity();

        auto pose = fp.pose;

        Eigen::Matrix3f head_rotation_matrix = pose.orientation.toRotationMatrix();

        constexpr int LEFT_EYE = 0;

        // Offset of eyeball from pose
        auto eyeball =
        Eigen::Vector3f((eye == LEFT_EYE ? -display_params::ipd / 2.0f : display_params::ipd / 2.0f), 0, 0);

        // Apply head rotation to eyeball offset vector
        eyeball = head_rotation_matrix * eyeball;

        // Apply head position to eyeball
        eyeball += pose.position;

        // Build our eye matrix from the pose's position + orientation.
        Eigen::Matrix4f eye_matrix   = Eigen::Matrix4f::Identity();
        eye_matrix.block<3, 1>(0, 3) = eyeball; // Set position to eyeball's position
        eye_matrix.block<3, 3>(0, 0) = pose.orientation.toRotationMatrix();

        // Objects' "view matrix" is inverse of eye matrix.
        auto view_matrix = eye_matrix.inverse();

        auto model_view = view_matrix * modelMatrix;
        UniformBufferObject *ubo = (UniformBufferObject *) uniform_buffer_allocation_infos[eye].pMappedData;
        memcpy(&ubo->model_view, &model_view, sizeof(model_view));
        memcpy(&ubo->proj, &basic_projection, sizeof(basic_projection));
    }

    void create_descriptor_set_layout() {
        VkDescriptorSetLayoutBinding ubo_layout_binding = {};
        ubo_layout_binding.binding = 0;
        ubo_layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        ubo_layout_binding.descriptorCount = 1;
        ubo_layout_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

        VkDescriptorSetLayoutCreateInfo layout_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        layout_info.bindingCount = 1;
        layout_info.pBindings = &ubo_layout_binding;

        VK_ASSERT_SUCCESS(vkCreateDescriptorSetLayout(ds->vk_device, &layout_info, nullptr, &descriptor_set_layout));
    }

    void create_uniform_buffers() {
        for (auto i = 0; i < 2; i++) {
            VkBufferCreateInfo buffer_info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
            buffer_info.size = sizeof(UniformBufferObject);
            buffer_info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

            VmaAllocationCreateInfo alloc_info = {};
            alloc_info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
            alloc_info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

            VK_ASSERT_SUCCESS(vmaCreateBuffer(vma_allocator, &buffer_info, &alloc_info, &uniform_buffers[i], &uniform_buffer_allocations[i],
                            &uniform_buffer_allocation_infos[i]));
        }
    }

    void create_descriptor_pool() {
        std::array<VkDescriptorPoolSize, 1> pool_sizes = {};
        pool_sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        pool_sizes[0].descriptorCount = 1;

        VkDescriptorPoolCreateInfo pool_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        pool_info.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
        pool_info.pPoolSizes = pool_sizes.data();
        pool_info.maxSets = 2;

        VK_ASSERT_SUCCESS(vkCreateDescriptorPool(ds->vk_device, &pool_info, nullptr, &descriptor_pool));
    }

    void create_descriptor_set() {
        VkDescriptorSetLayout layouts[] = { descriptor_set_layout, descriptor_set_layout };
        VkDescriptorSetAllocateInfo alloc_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        alloc_info.descriptorPool = descriptor_pool;
        alloc_info.descriptorSetCount = 2;
        alloc_info.pSetLayouts = layouts;

        VK_ASSERT_SUCCESS(vkAllocateDescriptorSets(ds->vk_device, &alloc_info, descriptor_sets.data()));

        std::array<VkDescriptorBufferInfo, 2> buffer_infos = {};
        for (auto i = 0; i < 2; i++) {
            buffer_infos[i].buffer = uniform_buffers[i];
            buffer_infos[i].offset = 0;
            buffer_infos[i].range = sizeof(UniformBufferObject);
        }

        std::array<VkWriteDescriptorSet, 2> descriptor_writes = {};

        for (auto i = 0; i < 2; i++) {
            descriptor_writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptor_writes[i].dstSet = descriptor_sets[i];
            descriptor_writes[i].dstBinding = 0;
            descriptor_writes[i].dstArrayElement = 0;
            descriptor_writes[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            descriptor_writes[i].descriptorCount = 1;
            descriptor_writes[i].pBufferInfo = &buffer_infos[i];
        }

        vkUpdateDescriptorSets(ds->vk_device, static_cast<uint32_t>(descriptor_writes.size()), descriptor_writes.data(), 0, nullptr);
    }

    void load_model() {
        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> materials;
        std::string warn, err;

        auto path = std::string(std::getenv("ILLIXR_DEMO_DATA")) + "/" + "scene.obj";
        if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.c_str())) {
            throw std::runtime_error(warn + err);
        }

        std::unordered_map<Vertex, uint32_t> unique_vertices{};

        for (const auto& shape : shapes) {
            for (const auto& index : shape.mesh.indices) {
                Vertex vertex{};

                vertex.pos = {
                        attrib.vertices[3 * index.vertex_index + 0] * 2,
                        attrib.vertices[3 * index.vertex_index + 1] * 2,
                        attrib.vertices[3 * index.vertex_index + 2] * 2
                };

                vertex.uv = {
                        attrib.texcoords[2 * index.texcoord_index + 0],
                        1.0f - attrib.texcoords[2 * index.texcoord_index + 1]
                };

                if (unique_vertices.count(vertex) == 0) {
                    unique_vertices[vertex] = static_cast<uint32_t>(vertices.size());
                    vertices.push_back(vertex);
                }

                indices.push_back(unique_vertices[vertex]);
            }
        }
    }

    void create_vertex_buffer() {
        VkBufferCreateInfo staging_buffer_info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        staging_buffer_info.size = sizeof(vertices[0]) * vertices.size();
        staging_buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

        VmaAllocationCreateInfo staging_alloc_info = {};
        staging_alloc_info.usage = VMA_MEMORY_USAGE_AUTO;
        staging_alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

        VkBuffer staging_buffer;
        VmaAllocation staging_buffer_allocation;
        VK_ASSERT_SUCCESS(vmaCreateBuffer(vma_allocator, &staging_buffer_info, &staging_alloc_info, &staging_buffer, &staging_buffer_allocation, nullptr));

        VkBufferCreateInfo buffer_info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        buffer_info.size = sizeof(vertices[0]) * vertices.size();
        buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

        VmaAllocationCreateInfo alloc_info = {};
        alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        VmaAllocation buffer_allocation;
        VK_ASSERT_SUCCESS(vmaCreateBuffer(vma_allocator, &buffer_info, &alloc_info, &vertex_buffer, &buffer_allocation, nullptr));

        void* mapped_data;
        VK_ASSERT_SUCCESS(vmaMapMemory(vma_allocator, staging_buffer_allocation, &mapped_data));
        memcpy(mapped_data, vertices.data(), sizeof(vertices[0]) * vertices.size());
        vmaUnmapMemory(vma_allocator, staging_buffer_allocation);

        VkCommandBuffer command_buffer = vulkan_utils::begin_one_time_command(ds->vk_device, command_pool);
        VkBufferCopy copy_region = {};
        copy_region.size = sizeof(vertices[0]) * vertices.size();
        vkCmdCopyBuffer(command_buffer, staging_buffer, vertex_buffer, 1, &copy_region);
        vulkan_utils::end_one_time_command(ds->vk_device, command_pool, ds->graphics_queue, command_buffer);

        vmaDestroyBuffer(vma_allocator, staging_buffer, staging_buffer_allocation);
    }

    void create_index_buffer() {
        VkBufferCreateInfo staging_buffer_info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        staging_buffer_info.size = sizeof(indices[0]) * indices.size();
        staging_buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

        VmaAllocationCreateInfo staging_alloc_info = {};
        staging_alloc_info.usage = VMA_MEMORY_USAGE_AUTO;
        staging_alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

        VkBuffer staging_buffer;
        VmaAllocation staging_buffer_allocation;
        VK_ASSERT_SUCCESS(vmaCreateBuffer(vma_allocator, &staging_buffer_info, &staging_alloc_info, &staging_buffer, &staging_buffer_allocation, nullptr));

        VkBufferCreateInfo buffer_info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        buffer_info.size = sizeof(indices[0]) * indices.size();
        buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

        VmaAllocationCreateInfo alloc_info = {};
        alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        VmaAllocation buffer_allocation;
        VK_ASSERT_SUCCESS(vmaCreateBuffer(vma_allocator, &buffer_info, &alloc_info, &index_buffer, &buffer_allocation, nullptr));

        void* mapped_data;
        VK_ASSERT_SUCCESS(vmaMapMemory(vma_allocator, staging_buffer_allocation, &mapped_data));
        memcpy(mapped_data, indices.data(), sizeof(indices[0]) * indices.size());
        vmaUnmapMemory(vma_allocator, staging_buffer_allocation);

        VkCommandBuffer command_buffer = vulkan_utils::begin_one_time_command(ds->vk_device, command_pool);
        VkBufferCopy copy_region = {};
        copy_region.size = sizeof(indices[0]) * indices.size();
        vkCmdCopyBuffer(command_buffer, staging_buffer, index_buffer, 1, &copy_region);
        vulkan_utils::end_one_time_command(ds->vk_device, command_pool, ds->graphics_queue, command_buffer);

        vmaDestroyBuffer(vma_allocator, staging_buffer, staging_buffer_allocation);
    }

    void create_pipeline(VkRenderPass render_pass, uint32_t subpass) {
        if (pipeline != VK_NULL_HANDLE) {
            throw std::runtime_error("timewarp_vk::create_pipeline: pipeline already created");
        }

        auto folder = std::string(SHADER_FOLDER);
        VkShaderModule vert = vulkan_utils::create_shader_module(ds->vk_device, vulkan_utils::read_file(folder + "/demo.vert.spv"));
        VkShaderModule frag = vulkan_utils::create_shader_module(ds->vk_device, vulkan_utils::read_file(folder + "/demo.frag.spv"));

        VkPipelineShaderStageCreateInfo vert_shader_stage_info = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        vert_shader_stage_info.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vert_shader_stage_info.module = vert;
        vert_shader_stage_info.pName = "main";

        VkPipelineShaderStageCreateInfo frag_shader_stage_info = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        frag_shader_stage_info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        frag_shader_stage_info.module = frag;
        frag_shader_stage_info.pName = "main";

        VkPipelineShaderStageCreateInfo shader_stages[] = { vert_shader_stage_info, frag_shader_stage_info };

        auto binding_description = Vertex::get_binding_description();
        auto attribute_descriptions = Vertex::get_attribute_descriptions();

        VkPipelineVertexInputStateCreateInfo vertex_input_info = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
        vertex_input_info.vertexBindingDescriptionCount = 1;
        vertex_input_info.vertexAttributeDescriptionCount = static_cast<uint32_t>(attribute_descriptions.size());
        vertex_input_info.pVertexBindingDescriptions = &binding_description;
        vertex_input_info.pVertexAttributeDescriptions = attribute_descriptions.data();

        VkPipelineInputAssemblyStateCreateInfo input_assembly = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
        input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        input_assembly.primitiveRestartEnable = VK_FALSE;

        VkViewport viewport = {};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(ds->swapchain_extent.width);
        viewport.height = static_cast<float>(ds->swapchain_extent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor = {};
        scissor.offset = { 0, 0 };
        scissor.extent = ds->swapchain_extent;

        VkPipelineViewportStateCreateInfo viewport_state = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
        viewport_state.viewportCount = 1;
        viewport_state.pViewports = &viewport;
        viewport_state.scissorCount = 1;
        viewport_state.pScissors = &scissor;

        VkPipelineRasterizationStateCreateInfo rasterizer = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.lineWidth = 1.0f;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.depthBiasEnable = VK_FALSE;

        VkPipelineMultisampleStateCreateInfo multisampling = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        
        VkPipelineColorBlendAttachmentState color_blend_attachment = {};
        color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        color_blend_attachment.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo color_blending = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
        color_blending.pAttachments = &color_blend_attachment;
        color_blending.attachmentCount = 1;

        VkPipelineLayoutCreateInfo pipeline_layout_info = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        pipeline_layout_info.setLayoutCount = 1;
        pipeline_layout_info.pSetLayouts = &descriptor_set_layout;

        VK_ASSERT_SUCCESS(vkCreatePipelineLayout(ds->vk_device, &pipeline_layout_info, nullptr, &pipeline_layout));

        VkPipelineDepthStencilStateCreateInfo depth_stencil = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
        depth_stencil.depthTestEnable = VK_TRUE;
        depth_stencil.depthWriteEnable = VK_TRUE;
        depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS;
        depth_stencil.depthBoundsTestEnable = VK_FALSE;
        depth_stencil.minDepthBounds = 0.0f;
        depth_stencil.maxDepthBounds = 1.0f;
        depth_stencil.stencilTestEnable = VK_FALSE;

        VkGraphicsPipelineCreateInfo pipeline_info = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        pipeline_info.stageCount = 2;
        pipeline_info.pStages = shader_stages;
        pipeline_info.pVertexInputState = &vertex_input_info;
        pipeline_info.pInputAssemblyState = &input_assembly;
        pipeline_info.pViewportState = &viewport_state;
        pipeline_info.pRasterizationState = &rasterizer;
        pipeline_info.pMultisampleState = &multisampling;
        pipeline_info.pColorBlendState = &color_blending;
        pipeline_info.pDepthStencilState = &depth_stencil;
        pipeline_info.layout = pipeline_layout;
        pipeline_info.renderPass = render_pass;
        pipeline_info.subpass = subpass;

        VK_ASSERT_SUCCESS(vkCreateGraphicsPipelines(ds->vk_device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline));

        vkDestroyShaderModule(ds->vk_device, vert, nullptr);
        vkDestroyShaderModule(ds->vk_device, frag, nullptr);
    }

    const std::shared_ptr<switchboard>                                sb;
    const std::shared_ptr<pose_prediction>                            pp;
    const std::shared_ptr<display_sink> ds = nullptr;
    const std::shared_ptr<const RelativeClock>                        _m_clock;
    const switchboard::reader<switchboard::event_wrapper<time_point>> _m_vsync;

    Eigen::Matrix4f basic_projection;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    std::array<std::vector<VkImageView>, 2> buffer_pool;

    VmaAllocator vma_allocator;
    VkCommandPool command_pool;
    VkCommandBuffer command_buffer;

    VkDescriptorSetLayout descriptor_set_layout;
    VkDescriptorPool descriptor_pool;
    std::array<VkDescriptorSet, 2> descriptor_sets;

    std::array<VkBuffer, 2> uniform_buffers;
    std::array<VmaAllocation, 2> uniform_buffer_allocations;
    std::array<VmaAllocationInfo, 2> uniform_buffer_allocation_infos;

    VkBuffer vertex_buffer;
    VkBuffer index_buffer;

    VkPipelineLayout pipeline_layout;
};

class vkdemo_plugin : public plugin {
public:
    vkdemo_plugin(const std::string& name, phonebook* pb)
        : plugin{name, pb},
        vkd{std::make_shared<vkdemo>(pb)} {
        pb->register_impl<app>(std::static_pointer_cast<vkdemo>(vkd));
    }

    virtual void start() override {
        vkd->initialize();
    }

private:
    std::shared_ptr<vkdemo> vkd;
};

PLUGIN_MAIN(vkdemo_plugin)
