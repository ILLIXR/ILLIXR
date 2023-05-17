#include <cassert>
#include <cstdint>
#include <iterator>
#include <sys/types.h>
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

#define STB_IMAGE_IMPLEMENTATION
#include "common/gl_util/lib/stb_image.h"

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

struct Texture {
    VkImage image;
    VmaAllocation image_memory;
    VkImageView image_view;
};

struct Model {
    int texture_index;
    uint32_t vertex_offset;
    uint32_t index_offset;
    uint32_t index_count;
};

struct ModelPushConstant {
    int texture_index;
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

        command_pool = vulkan_utils::create_command_pool(ds->vk_device, ds->graphics_queue_family);
        command_buffer = vulkan_utils::create_command_buffer(ds->vk_device, command_pool);
        load_model();
        bake_models();
        create_texture_sampler();
        create_descriptor_set_layout();
        create_uniform_buffers();
        create_descriptor_pool();
        create_descriptor_set();
        create_vertex_buffer();
        create_index_buffer();
        vertices.clear();
        indices.clear();

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
        
        for (auto& model : models) {
            ModelPushConstant push_constant{};
            push_constant.texture_index = texture_map[model.texture_index];
            vkCmdPushConstants(commandBuffer, pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                               sizeof(ModelPushConstant), &push_constant);
            vkCmdDrawIndexed(commandBuffer, model.index_count, 1, model.index_offset, 0, 0);
        }
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

        Eigen::Matrix4f model_view = view_matrix * modelMatrix;

        UniformBufferObject *ubo = (UniformBufferObject *) uniform_buffer_allocation_infos[eye].pMappedData;
        memcpy(&ubo->model_view, &model_view, sizeof(model_view));
        memcpy(&ubo->proj, &basic_projection, sizeof(basic_projection));
    }

    void bake_models() {
        for (auto i = 0; i < textures.size(); i++) {
            if (textures[i].image_view == VK_NULL_HANDLE) {
                continue;
            }
            VkDescriptorImageInfo image_info = {};
            image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            image_info.imageView = textures[i].image_view;
            image_info.sampler = nullptr;
            texture_map.insert(std::make_pair(i, image_infos.size()));
            image_infos.push_back(image_info);
        }
    }

    void create_descriptor_set_layout() {
        VkDescriptorSetLayoutBinding ubo_layout_binding = {};
        ubo_layout_binding.binding = 0;
        ubo_layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        ubo_layout_binding.descriptorCount = 1;
        ubo_layout_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

        VkDescriptorSetLayoutBinding sampler_layout_binding = {};
        sampler_layout_binding.binding = 1;
        sampler_layout_binding.descriptorCount = 1;
        sampler_layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        sampler_layout_binding.pImmutableSamplers = nullptr;
        sampler_layout_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutBinding sampled_image_layout_binding = {};
        sampled_image_layout_binding.binding = 2;
        sampled_image_layout_binding.descriptorCount = texture_map.size();
        sampled_image_layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        sampled_image_layout_binding.pImmutableSamplers = nullptr;
        sampled_image_layout_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layout_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        layout_info.bindingCount = 3;
        VkDescriptorSetLayoutBinding bindings[] = {ubo_layout_binding, sampler_layout_binding,
                                                   sampled_image_layout_binding};
        layout_info.pBindings = bindings;

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

    void create_texture_sampler() {
        VkSamplerCreateInfo sampler_info = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        sampler_info.magFilter = VK_FILTER_LINEAR;
        sampler_info.minFilter = VK_FILTER_LINEAR;
        sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_info.anisotropyEnable = VK_FALSE;
        // sampler_info.maxAnisotropy = 16;
        sampler_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        sampler_info.unnormalizedCoordinates = VK_FALSE;
        sampler_info.compareEnable = VK_FALSE;
        sampler_info.compareOp = VK_COMPARE_OP_ALWAYS;
        sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

        VK_ASSERT_SUCCESS(vkCreateSampler(ds->vk_device, &sampler_info, nullptr, &texture_sampler));
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

        std::vector<VkWriteDescriptorSet> image_descriptor_writes = {};
        for (auto i = 0; i < 2; i++) {
            VkWriteDescriptorSet descriptor_write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            descriptor_write.dstSet = descriptor_sets[i];
            descriptor_write.dstBinding = 1;
            descriptor_write.dstArrayElement = 0;
            descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
            descriptor_write.descriptorCount = 1;
            VkDescriptorImageInfo image_info = { texture_sampler };
            descriptor_write.pImageInfo = &image_info;
            image_descriptor_writes.push_back(descriptor_write);

            assert(image_infos.size() > 0);
            descriptor_write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            descriptor_write.dstSet = descriptor_sets[i];
            descriptor_write.dstBinding = 2;
            descriptor_write.dstArrayElement = 0;
            descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            descriptor_write.descriptorCount = static_cast<uint32_t>(image_infos.size());
            descriptor_write.pImageInfo = image_infos.data();
            image_descriptor_writes.push_back(descriptor_write);
        }

        vkUpdateDescriptorSets(ds->vk_device, static_cast<uint32_t>(image_descriptor_writes.size()), image_descriptor_writes.data(), 0, nullptr);
    }

    void load_texture(std::string path, int i) {
        int width, height, channels;
        auto data = stbi_load(path.c_str(), &width, &height, &channels, 0);
        std::cout << "Loaded texture " << path << " with dimensions " << width << "x" << height << " and " << channels << " channels" << std::endl;
        if (data == nullptr) {
            throw std::runtime_error("Failed to load texture image!");
        }

        // add alpha channel if image has no alpha channel
        if (channels == 3) {
            auto new_data = new unsigned char[width * height * 4];
            for (auto y = 0; y < height; y++) {
                for (auto x = 0; x < width; x++) {
                    new_data[(y * width + x) * 4 + 0] = data[(y * width + x) * 3 + 0];
                    new_data[(y * width + x) * 4 + 1] = data[(y * width + x) * 3 + 1];
                    new_data[(y * width + x) * 4 + 2] = data[(y * width + x) * 3 + 2];
                    new_data[(y * width + x) * 4 + 3] = 255;
                }
            }
            stbi_image_free(data);
            data = new_data;
            channels = 4;
        }

        VkDeviceSize image_size = width * height * channels;

        VkBuffer staging_buffer;
        VmaAllocation staging_buffer_allocation;
        VmaAllocationInfo staging_buffer_allocation_info;

        VkBufferCreateInfo buffer_info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        buffer_info.size = image_size;
        buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

        VmaAllocationCreateInfo alloc_info = {};
        alloc_info.usage = VMA_MEMORY_USAGE_AUTO;
        alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VK_ASSERT_SUCCESS(vmaCreateBuffer(vma_allocator, &buffer_info, &alloc_info, &staging_buffer, &staging_buffer_allocation,
                            &staging_buffer_allocation_info));
        
        memcpy(staging_buffer_allocation_info.pMappedData, data, static_cast<size_t>(image_size));

        stbi_image_free(data);

        VkImageCreateInfo image_info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        image_info.imageType = VK_IMAGE_TYPE_2D;
        image_info.extent.width = static_cast<uint32_t>(width);
        image_info.extent.height = static_cast<uint32_t>(height);
        image_info.extent.depth = 1;
        image_info.mipLevels = 1;
        image_info.arrayLayers = 1;
        image_info.format = VK_FORMAT_R8G8B8A8_SRGB;
        image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;

        VmaAllocationCreateInfo image_alloc_info = {};
        image_alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        VK_ASSERT_SUCCESS(vmaCreateImage(vma_allocator, &image_info, &image_alloc_info, &textures[i].image, &textures[i].image_memory,
                            nullptr));

        image_layout_transition(textures[i].image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        
        vulkan_utils::copy_buffer_to_image(ds->vk_device, ds->graphics_queue, command_pool, staging_buffer, textures[i].image, width, height);

        image_layout_transition(textures[i].image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                                
        vmaDestroyBuffer(vma_allocator, staging_buffer, staging_buffer_allocation);

        VkImageViewCreateInfo view_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        view_info.image = textures[i].image;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = VK_FORMAT_R8G8B8A8_SRGB;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.baseMipLevel = 0;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount = 1;

        VK_ASSERT_SUCCESS(vkCreateImageView(ds->vk_device, &view_info, nullptr, &textures[i].image_view));
    }

    void image_layout_transition(VkImage image, VkFormat format, VkImageLayout old_layout, VkImageLayout new_layout) {
        VkCommandBuffer command_buffer = vulkan_utils::begin_one_time_command(ds->vk_device, command_pool);

        VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        barrier.oldLayout = old_layout;
        barrier.newLayout = new_layout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;

        if (new_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        } else {
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        }

        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        VkPipelineStageFlags source_stage;
        VkPipelineStageFlags destination_stage;

        if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

            source_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destination_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        } else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
                   new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            source_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            destination_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        } else if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED &&
                   new_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

            source_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destination_stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        } else {
            throw std::invalid_argument("Unsupported layout transition!");
        }

        vkCmdPipelineBarrier(command_buffer, source_stage, destination_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        vulkan_utils::end_one_time_command(ds->vk_device, command_pool, ds->graphics_queue, command_buffer);
    }

    void load_model() {
        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> materials;
        std::string warn, err;

        auto path = std::string(std::getenv("ILLIXR_DEMO_DATA")) + "/" + "scene.obj";
        if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.c_str(), std::getenv("ILLIXR_DEMO_DATA"))) {
            throw std::runtime_error(warn + err);
        }

        textures.resize(materials.size());

        for (auto i = 0; i < materials.size(); i++) {
            auto material = materials[i];
            if (material.diffuse_texname.length() > 0) {
                auto path = std::string(std::getenv("ILLIXR_DEMO_DATA")) + "/" + material.diffuse_texname;
                load_texture(path, i);
            }
        }

        std::cout << "Loaded " << textures.size() << " textures" << std::endl;

        std::unordered_map<Vertex, uint32_t> unique_vertices{};
        for (const auto& shape : shapes) {
            Model model{};
            model.index_offset = static_cast<uint32_t>(indices.size());
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
            if (shape.mesh.material_ids.size() > 0) {
                model.texture_index = shape.mesh.material_ids[0];
            } else {
                model.texture_index = -1;
            }
            model.index_count = static_cast<uint32_t>(shape.mesh.indices.size());
            models.push_back(model);
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

        VkPushConstantRange push_constant_range = {};
        push_constant_range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        push_constant_range.offset = 0;
        push_constant_range.size = sizeof(ModelPushConstant);

        pipeline_layout_info.pushConstantRangeCount = 1;
        pipeline_layout_info.pPushConstantRanges = &push_constant_range;

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
    std::vector<Model> models;
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

    std::vector<VkDescriptorImageInfo> image_infos;
    std::vector<Texture> textures;
    VkSampler texture_sampler;
    std::map<uint32_t, uint32_t> texture_map;
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
