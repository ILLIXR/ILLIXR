#include <future>
#include <iostream>
#include <mutex>
#include <stack>

#define VMA_IMPLEMENTATION

#include "illixr/data_format.hpp"
#include "illixr/global_module_defs.hpp"
#include "illixr/math_util.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/pose_prediction.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"
#include "illixr/vk_util/display_sink.hpp"
#include "illixr/vk_util/render_pass.hpp"
#include "illixr/vk_util/vulkan_utils.hpp"
#include "utils/hmd.hpp"

#include <vulkan/vulkan_core.h>

using namespace ILLIXR;

struct Vertex {
    glm::vec3 pos;
    glm::vec2 uv0;
    glm::vec2 uv1;
    glm::vec2 uv2;

    static VkVertexInputBindingDescription get_binding_description() {
        VkVertexInputBindingDescription binding_description = {};
        binding_description.binding                         = 0;              // index of the binding in the array of bindings
        binding_description.stride                          = sizeof(Vertex); // number of bytes from one entry to the next
        binding_description.inputRate                       = VK_VERTEX_INPUT_RATE_VERTEX; // no instancing

        return binding_description;
    }

    static std::array<VkVertexInputAttributeDescription, 4> get_attribute_descriptions() {
        std::array<VkVertexInputAttributeDescription, 4> attribute_descriptions = {};

        // position
        attribute_descriptions[0].binding  = 0;                          // which binding the per-vertex data comes from
        attribute_descriptions[0].location = 0;                          // location directive of the input in the vertex shader
        attribute_descriptions[0].format   = VK_FORMAT_R32G32B32_SFLOAT; // format of the data
        attribute_descriptions[0].offset =
            offsetof(Vertex, pos); // number of bytes since the start of the per-vertex data to read from

        // uv0
        attribute_descriptions[1].binding  = 0;
        attribute_descriptions[1].location = 1;
        attribute_descriptions[1].format   = VK_FORMAT_R32G32_SFLOAT;
        attribute_descriptions[1].offset   = offsetof(Vertex, uv0);

        // uv1
        attribute_descriptions[2].binding  = 0;
        attribute_descriptions[2].location = 2;
        attribute_descriptions[2].format   = VK_FORMAT_R32G32_SFLOAT;
        attribute_descriptions[2].offset   = offsetof(Vertex, uv1);

        // uv2
        attribute_descriptions[3].binding  = 0;
        attribute_descriptions[3].location = 3;
        attribute_descriptions[3].format   = VK_FORMAT_R32G32_SFLOAT;
        attribute_descriptions[3].offset   = offsetof(Vertex, uv2);

        return attribute_descriptions;
    }
};

struct UniformBufferObject {
    glm::mat4 timewarp_start_transform;
    glm::mat4 timewarp_end_transform;
};

class timewarp_vk : public timewarp {
public:
    explicit timewarp_vk(const phonebook* const pb)
        : pb{pb}
        , sb{pb->lookup_impl<switchboard>()}
        , pp{pb->lookup_impl<pose_prediction>()}
        , disable_warp{ILLIXR::str_to_bool(ILLIXR::getenv_or("ILLIXR_TIMEWARP_DISABLE", "False"))} { }

    void initialize() {
        ds = pb->lookup_impl<display_sink>();

        if (ds->vma_allocator) {
            this->vma_allocator = ds->vma_allocator;
        } else {
            this->vma_allocator = vulkan_utils::create_vma_allocator(ds->vk_instance, ds->vk_physical_device, ds->vk_device);
            deletion_queue.emplace([=]() {
                vmaDestroyAllocator(vma_allocator);
            });
        }

        generate_distortion_data();
        command_pool   = vulkan_utils::create_command_pool(ds->vk_device, ds->graphics_queue_family);
        command_buffer = vulkan_utils::create_command_buffer(ds->vk_device, command_pool);
        deletion_queue.emplace([=]() {
            vkDestroyCommandPool(ds->vk_device, command_pool, nullptr);
        });
        create_vertex_buffer();
        create_index_buffer();
        create_descriptor_set_layout();
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
        create_pipeline(render_pass, subpass);
    }

    void partial_destroy() {
        vkDestroyPipeline(ds->vk_device, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;

        vkDestroyPipelineLayout(ds->vk_device, pipeline_layout, nullptr);
        pipeline_layout = VK_NULL_HANDLE;

        vkDestroyDescriptorPool(ds->vk_device, descriptor_pool, nullptr);
        descriptor_pool = VK_NULL_HANDLE;
    }

    void update_uniforms(const pose_type& render_pose) override {
        num_update_uniforms_calls++;

        // Generate "starting" view matrix, from the pose sampled at the time of rendering the frame
        Eigen::Matrix4f viewMatrix   = Eigen::Matrix4f::Identity();
        viewMatrix.block(0, 0, 3, 3) = render_pose.orientation.toRotationMatrix();

        // We simulate two asynchronous view matrices, one at the beginning of
        // display refresh, and one at the end of display refresh. The
        // distortion shader will lerp between these two predictive view
        // transformations as it renders across the horizontal view,
        // compensating for display panel refresh delay (wow!)
        Eigen::Matrix4f viewMatrixBegin = Eigen::Matrix4f::Identity();
        Eigen::Matrix4f viewMatrixEnd   = Eigen::Matrix4f::Identity();

        const pose_type latest_pose       = disable_warp ? render_pose : pp->get_fast_pose().pose;
        viewMatrixBegin.block(0, 0, 3, 3) = latest_pose.orientation.toRotationMatrix();

        // TODO: We set the "end" pose to the same as the beginning pose, but this really should be the pose for
        // `display_period` later
        viewMatrixEnd = viewMatrixBegin;

        // Calculate the timewarp transformation matrices. These are a product
        // of the last-known-good view matrix and the predictive transforms.
        Eigen::Matrix4f timeWarpStartTransform4x4;
        Eigen::Matrix4f timeWarpEndTransform4x4;

        // Calculate timewarp transforms using predictive view transforms
        calculate_timewarp_transform(timeWarpStartTransform4x4, basicProjection, viewMatrix, viewMatrixBegin);
        calculate_timewarp_transform(timeWarpEndTransform4x4, basicProjection, viewMatrix, viewMatrixEnd);

        auto* ubo = (UniformBufferObject*) uniform_alloc_info.pMappedData;
        memcpy(&ubo->timewarp_start_transform, timeWarpStartTransform4x4.data(), sizeof(glm::mat4));
        memcpy(&ubo->timewarp_end_transform, timeWarpEndTransform4x4.data(), sizeof(glm::mat4));
    }

    void record_command_buffer(VkCommandBuffer commandBuffer, int buffer_ind, bool left) override {
        num_record_calls++;

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertex_buffer, offsets);
        // for (int eye = 0; eye < HMD::NUM_EYES; eye++) {
        //     vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1,
        //     &descriptor_sets[eye][buffer_ind], 0, nullptr); vkCmdBindIndexBuffer(commandBuffer, index_buffer, 0,
        //     VK_INDEX_TYPE_UINT32); vkCmdDrawIndexed(commandBuffer, num_distortion_indices, 1, 0, num_distortion_vertices *
        //     eye, 0);
        // }
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1,
                                &descriptor_sets[!left][buffer_ind], 0, nullptr);
        vkCmdBindIndexBuffer(commandBuffer, index_buffer, 0, VK_INDEX_TYPE_UINT32);
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
    void create_vertex_buffer() {
        VkBufferCreateInfo staging_buffer_info = {
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, // sType
            nullptr,                              // pNext
            0,                                    // flags
            0,                                    // size
            0,                                    // usage
            {},                                   // sharingMode
            0,                                    // queueFamilyIndexCount
            nullptr                               // pQueueFamilyIndices
        };
        staging_buffer_info.size  = sizeof(Vertex) * num_distortion_vertices * HMD::NUM_EYES;
        staging_buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

        VmaAllocationCreateInfo staging_alloc_info = {};
        staging_alloc_info.usage                   = VMA_MEMORY_USAGE_AUTO;
        staging_alloc_info.flags                   = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

        VkBuffer      staging_buffer;
        VmaAllocation staging_alloc;
        VK_ASSERT_SUCCESS(
            vmaCreateBuffer(vma_allocator, &staging_buffer_info, &staging_alloc_info, &staging_buffer, &staging_alloc, nullptr))

        VkBufferCreateInfo buffer_info = {
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, // sType
            nullptr,                              // pNext
            0,                                    // flags
            0,                                    // size
            0,                                    // usage
            {},                                   // sharingMode
            0,                                    // queueFamilyIndexCount
            nullptr                               // pQueueFamilyIndices
        };
        buffer_info.size  = sizeof(Vertex) * num_distortion_vertices * HMD::NUM_EYES;
        buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

        VmaAllocationCreateInfo alloc_info = {};
        alloc_info.usage                   = VMA_MEMORY_USAGE_GPU_ONLY;

        VmaAllocation vertex_alloc;
        VK_ASSERT_SUCCESS(vmaCreateBuffer(vma_allocator, &buffer_info, &alloc_info, &vertex_buffer, &vertex_alloc, nullptr))

        std::vector<Vertex> vertices;
        vertices.resize(num_distortion_vertices * HMD::NUM_EYES);
        for (size_t i = 0; i < num_distortion_vertices * HMD::NUM_EYES; i++) {
            vertices[i].pos = {distortion_positions[i].x, distortion_positions[i].y, distortion_positions[i].z};
            vertices[i].uv0 = {distortion_uv0[i].u, distortion_uv0[i].v};
            vertices[i].uv1 = {distortion_uv1[i].u, distortion_uv1[i].v};
            vertices[i].uv2 = {distortion_uv2[i].u, distortion_uv2[i].v};
        }

        void* mapped_data;
        VK_ASSERT_SUCCESS(vmaMapMemory(vma_allocator, staging_alloc, &mapped_data))
        memcpy(mapped_data, vertices.data(), sizeof(Vertex) * num_distortion_vertices * HMD::NUM_EYES);
        vmaUnmapMemory(vma_allocator, staging_alloc);

        VkCommandBuffer command_buffer_local = vulkan_utils::begin_one_time_command(ds->vk_device, command_pool);
        VkBufferCopy    copy_region          = {};
        copy_region.size                     = sizeof(Vertex) * num_distortion_vertices * HMD::NUM_EYES;
        vkCmdCopyBuffer(command_buffer_local, staging_buffer, vertex_buffer, 1, &copy_region);
        vulkan_utils::end_one_time_command(ds->vk_device, command_pool, ds->graphics_queue, command_buffer_local);

        vmaDestroyBuffer(vma_allocator, staging_buffer, staging_alloc);

        deletion_queue.emplace([=]() {
            vmaDestroyBuffer(vma_allocator, vertex_buffer, vertex_alloc);
        });
    }

    void create_index_buffer() {
        VkBufferCreateInfo staging_buffer_info = {
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, // sType
            nullptr,                              // pNext
            0,                                    // flags
            0,                                    // size
            0,                                    // usage
            {},                                   // sharingMode
            0,                                    // queueFamilyIndexCount
            nullptr                               // pQueueFamilyIndices
        };
        staging_buffer_info.size  = sizeof(uint32_t) * num_distortion_indices;
        staging_buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

        VmaAllocationCreateInfo staging_alloc_info = {};
        staging_alloc_info.usage                   = VMA_MEMORY_USAGE_AUTO;
        staging_alloc_info.flags                   = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

        VkBuffer      staging_buffer;
        VmaAllocation staging_alloc;
        VK_ASSERT_SUCCESS(
            vmaCreateBuffer(vma_allocator, &staging_buffer_info, &staging_alloc_info, &staging_buffer, &staging_alloc, nullptr))

        VkBufferCreateInfo buffer_info = {
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, // sType
            nullptr,                              // pNext
            0,                                    // flags
            0,                                    // size
            0,                                    // usage
            {},                                   // sharingMode
            0,                                    // queueFamilyIndexCount
            nullptr                               // pQueueFamilyIndices
        };
        buffer_info.size  = sizeof(uint32_t) * num_distortion_indices;
        buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

        VmaAllocationCreateInfo alloc_info = {};
        alloc_info.usage                   = VMA_MEMORY_USAGE_GPU_ONLY;

        VmaAllocation index_alloc;
        VK_ASSERT_SUCCESS(vmaCreateBuffer(vma_allocator, &buffer_info, &alloc_info, &index_buffer, &index_alloc, nullptr))

        void* mapped_data;
        VK_ASSERT_SUCCESS(vmaMapMemory(vma_allocator, staging_alloc, &mapped_data))
        memcpy(mapped_data, distortion_indices.data(), sizeof(uint32_t) * num_distortion_indices);
        vmaUnmapMemory(vma_allocator, staging_alloc);

        VkCommandBuffer command_buffer_local = vulkan_utils::begin_one_time_command(ds->vk_device, command_pool);
        VkBufferCopy    copy_region          = {};
        copy_region.size                     = sizeof(uint32_t) * num_distortion_indices;
        vkCmdCopyBuffer(command_buffer_local, staging_buffer, index_buffer, 1, &copy_region);
        vulkan_utils::end_one_time_command(ds->vk_device, command_pool, ds->graphics_queue, command_buffer_local);

        vmaDestroyBuffer(vma_allocator, staging_buffer, staging_alloc);

        deletion_queue.emplace([=]() {
            vmaDestroyBuffer(vma_allocator, index_buffer, index_alloc);
        });
    }

    void generate_distortion_data() {
        // Generate reference HMD and physical body dimensions
        HMD::GetDefaultHmdInfo(display_params::width_pixels, display_params::height_pixels, display_params::width_meters,
                               display_params::height_meters, display_params::lens_separation,
                               display_params::meters_per_tan_angle, display_params::aberration, hmd_info);

        // Construct timewarp meshes and other data
        build_timewarp(hmd_info);
    }

    void create_texture_sampler() {
        VkSamplerCreateInfo samplerInfo = {
            VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, // sType
            nullptr,                               // pNext
            0,                                     // flags
            {},                                    // magFilter
            {},                                    // minFilter
            {},                                    // mipmapMode
            {},                                    // addressModeU
            {},                                    // addressModeV
            {},                                    // addressModeW
            0.f,                                   // mipLodBias
            0,                                     // anisotropyEnable
            0.f,                                   // maxAnisotropy
            0,                                     // compareEnable
            {},                                    // compareOp
            0.f,                                   // minLod
            0.f,                                   // maxLod
            {},                                    // borderColor
            0                                      // unnormalizedCoordinates
        };
        samplerInfo.magFilter = VK_FILTER_LINEAR; // how to interpolate texels that are magnified on screen
        samplerInfo.minFilter = VK_FILTER_LINEAR;

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

    void create_descriptor_set_layout() {
        VkDescriptorSetLayoutBinding uboLayoutBinding = {};
        uboLayoutBinding.binding                      = 0; // binding number in the shader
        uboLayoutBinding.descriptorType               = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboLayoutBinding.descriptorCount              = 1;
        uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT; // shader stages that can access the descriptor

        VkDescriptorSetLayoutBinding samplerLayoutBinding = {};
        samplerLayoutBinding.binding                      = 1;
        samplerLayoutBinding.descriptorType               = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        samplerLayoutBinding.descriptorCount              = 1;
        samplerLayoutBinding.stageFlags                   = VK_SHADER_STAGE_FRAGMENT_BIT;

        std::array<VkDescriptorSetLayoutBinding, 2> bindings   = {uboLayoutBinding, samplerLayoutBinding};
        VkDescriptorSetLayoutCreateInfo             layoutInfo = {};
        layoutInfo.sType                                       = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount                                = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data(); // array of VkDescriptorSetLayoutBinding structs

        VK_ASSERT_SUCCESS(vkCreateDescriptorSetLayout(ds->vk_device, &layoutInfo, nullptr, &descriptor_set_layout))
        deletion_queue.emplace([=]() {
            vkDestroyDescriptorSetLayout(ds->vk_device, descriptor_set_layout, nullptr);
        });
    }

    void create_uniform_buffer() {
        VkBufferCreateInfo bufferInfo = {
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, // sType
            nullptr,                              // pNext
            0,                                    // flags
            0,                                    // size
            0,                                    // usage
            {},                                   // sharingMode
            0,                                    // queueFamilyIndexCount
            nullptr                               // pQueueFamilyIndices
        };
        bufferInfo.size  = sizeof(UniformBufferObject);
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

        VmaAllocationCreateInfo createInfo = {};
        createInfo.usage                   = VMA_MEMORY_USAGE_AUTO;
        createInfo.flags         = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
        createInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

        VK_ASSERT_SUCCESS(
            vmaCreateBuffer(vma_allocator, &bufferInfo, &createInfo, &uniform_buffer, &uniform_alloc, &uniform_alloc_info))
        deletion_queue.emplace([=]() {
            vmaDestroyBuffer(vma_allocator, uniform_buffer, uniform_alloc);
        });
    }

    void create_descriptor_pool() {
        std::array<VkDescriptorPoolSize, 2> poolSizes = {};
        poolSizes[0].type                             = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSizes[0].descriptorCount                  = 2;
        poolSizes[1].type                             = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSizes[1].descriptorCount                  = 2;

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
            std::vector<VkDescriptorSetLayout> layouts   = {buffer_pool[0].size(), descriptor_set_layout};
            VkDescriptorSetAllocateInfo        allocInfo = {
                VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, // sType
                nullptr,                                        // pNext
                {},                                             // descriptorPool
                0,                                              // descriptorSetCount
                nullptr                                         // pSetLayouts
            };
            allocInfo.descriptorPool     = descriptor_pool;
            allocInfo.descriptorSetCount = buffer_pool[0].size();
            allocInfo.pSetLayouts        = layouts.data();

            descriptor_sets[eye].resize(buffer_pool[0].size());
            VK_ASSERT_SUCCESS(vkAllocateDescriptorSets(ds->vk_device, &allocInfo, descriptor_sets[eye].data()))

            for (size_t i = 0; i < buffer_pool[0].size(); i++) {
                VkDescriptorBufferInfo bufferInfo = {};
                bufferInfo.buffer                 = uniform_buffer;
                bufferInfo.offset                 = 0;
                bufferInfo.range                  = sizeof(UniformBufferObject);

                VkDescriptorImageInfo imageInfo = {};
                imageInfo.imageLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imageInfo.imageView             = buffer_pool[eye][i];
                imageInfo.sampler               = fb_sampler;

                std::array<VkWriteDescriptorSet, 2> descriptorWrites = {};

                descriptorWrites[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                descriptorWrites[0].dstSet          = descriptor_sets[eye][i];
                descriptorWrites[0].dstBinding      = 0;
                descriptorWrites[0].dstArrayElement = 0;
                descriptorWrites[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                descriptorWrites[0].descriptorCount = 1;
                descriptorWrites[0].pBufferInfo     = &bufferInfo;

                descriptorWrites[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                descriptorWrites[1].dstSet          = descriptor_sets[eye][i];
                descriptorWrites[1].dstBinding      = 1;
                descriptorWrites[1].dstArrayElement = 0;
                descriptorWrites[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                descriptorWrites[1].descriptorCount = 1;
                descriptorWrites[1].pImageInfo      = &imageInfo;

                vkUpdateDescriptorSets(ds->vk_device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(),
                                       0, nullptr);
            }
        }
    }

    VkPipeline create_pipeline(VkRenderPass render_pass, [[maybe_unused]] uint32_t subpass) {
        if (pipeline != VK_NULL_HANDLE) {
            throw std::runtime_error("timewarp_vk::create_pipeline: pipeline already created");
        }

        VkDevice device = ds->vk_device;

        auto           folder = std::string(SHADER_FOLDER);
        VkShaderModule vert   = vulkan_utils::create_shader_module(device, vulkan_utils::read_file(folder + "/tw.vert.spv"));
        VkShaderModule frag   = vulkan_utils::create_shader_module(device, vulkan_utils::read_file(folder + "/tw.frag.spv"));

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

        auto bindingDescription    = Vertex::get_binding_description();
        auto attributeDescriptions = Vertex::get_attribute_descriptions();

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

        // use dynamic state instead
        // VkViewport viewport = {};
        // viewport.x = 0.0f;
        // viewport.y = 0.0f;
        // viewport.width = ds->swapchain_extent.width;
        // viewport.height = ds->swapchain_extent.height;
        // viewport.minDepth = 0.0f;
        // viewport.maxDepth = 1.0f;

        // VkRect2D scissor = {};
        // scissor.offset = {0, 0};
        // scissor.extent = ds->swapchain_extent;

        // VkPipelineViewportStateCreateInfo viewportStateCreateInfo = {};
        // viewportStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        // viewportStateCreateInfo.viewportCount = 1;
        // viewportStateCreateInfo.pViewports = &viewport;
        // viewportStateCreateInfo.scissorCount = 1;
        // viewportStateCreateInfo.pScissors = &scissor;

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
        pipelineLayoutInfo.pSetLayouts                = &descriptor_set_layout;

        VK_ASSERT_SUCCESS(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipeline_layout))

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

        pipelineInfo.layout     = pipeline_layout;
        pipelineInfo.renderPass = render_pass;
        pipelineInfo.subpass    = 0;

        VK_ASSERT_SUCCESS(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline))

        vkDestroyShaderModule(device, vert, nullptr);
        vkDestroyShaderModule(device, frag, nullptr);
        return pipeline;
    }

    void build_timewarp(HMD::hmd_info_t& hmdInfo) {
        // Calculate the number of vertices+indices in the distortion mesh.
        num_distortion_vertices = (hmdInfo.eyeTilesHigh + 1) * (hmdInfo.eyeTilesWide + 1);
        num_distortion_indices  = hmdInfo.eyeTilesHigh * hmdInfo.eyeTilesWide * 6;

        // Allocate memory for the elements/indices array.
        distortion_indices.resize(num_distortion_indices);

        // This is just a simple grid/plane index array, nothing fancy.
        // Same for both eye distortions, too!
        for (int y = 0; y < hmdInfo.eyeTilesHigh; y++) {
            for (int x = 0; x < hmdInfo.eyeTilesWide; x++) {
                const int offset = (y * hmdInfo.eyeTilesWide + x) * 6;

                distortion_indices[offset + 0] = ((y + 0) * (hmdInfo.eyeTilesWide + 1) + (x + 0));
                distortion_indices[offset + 1] = ((y + 1) * (hmdInfo.eyeTilesWide + 1) + (x + 0));
                distortion_indices[offset + 2] = ((y + 0) * (hmdInfo.eyeTilesWide + 1) + (x + 1));

                distortion_indices[offset + 3] = ((y + 0) * (hmdInfo.eyeTilesWide + 1) + (x + 1));
                distortion_indices[offset + 4] = ((y + 1) * (hmdInfo.eyeTilesWide + 1) + (x + 0));
                distortion_indices[offset + 5] = ((y + 1) * (hmdInfo.eyeTilesWide + 1) + (x + 1));
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
        HMD::BuildDistortionMeshes(distort_coords, hmdInfo);

        // Allocate memory for position and UV CPU buffers.
        const std::size_t num_elems_pos_uv = HMD::NUM_EYES * num_distortion_vertices;
        distortion_positions.resize(num_elems_pos_uv);
        distortion_uv0.resize(num_elems_pos_uv);
        distortion_uv1.resize(num_elems_pos_uv);
        distortion_uv2.resize(num_elems_pos_uv);

        for (int eye = 0; eye < HMD::NUM_EYES; eye++) {
            for (int y = 0; y <= hmdInfo.eyeTilesHigh; y++) {
                for (int x = 0; x <= hmdInfo.eyeTilesWide; x++) {
                    const int index = y * (hmdInfo.eyeTilesWide + 1) + x;

                    // Set the physical distortion mesh coordinates. These are rectangular/gridlike, not distorted.
                    // The distortion is handled by the UVs, not the actual mesh coordinates!
                    // distortion_positions[eye * num_distortion_vertices + index].x = (-1.0f + eye + ((float) x /
                    // hmdInfo.eyeTilesWide));
                    distortion_positions[eye * num_distortion_vertices + index].x =
                        (-1.0f + (static_cast<float>(x) / static_cast<float>(hmdInfo.eyeTilesWide)));

                    // flip the y coordinates for Vulkan texture
                    distortion_positions[eye * num_distortion_vertices + index].y =
                        (input_texture_vulkan_coordinates ? -1.0f : 1.0f) *
                        (-1.0f +
                         2.0f * (static_cast<float>(hmdInfo.eyeTilesHigh - y) / static_cast<float>(hmdInfo.eyeTilesHigh)) *
                             (static_cast<float>(hmdInfo.eyeTilesHigh * hmdInfo.tilePixelsHigh) /
                              static_cast<float>(hmdInfo.displayPixelsHigh)));
                    distortion_positions[eye * num_distortion_vertices + index].z = 0.0f;

                    // Use the previously-calculated distort_coords to set the UVs on the distortion mesh
                    distortion_uv0[eye * num_distortion_vertices + index].u = distort_coords[eye][0][index].x;
                    distortion_uv0[eye * num_distortion_vertices + index].v = distort_coords[eye][0][index].y;
                    distortion_uv1[eye * num_distortion_vertices + index].u = distort_coords[eye][1][index].x;
                    distortion_uv1[eye * num_distortion_vertices + index].v = distort_coords[eye][1][index].y;
                    distortion_uv2[eye * num_distortion_vertices + index].u = distort_coords[eye][2][index].x;
                    distortion_uv2[eye * num_distortion_vertices + index].v = distort_coords[eye][2][index].y;
                }
            }
        }

        // Construct perspective projection matrix
        math_util::projection_fov(&basicProjection, display_params::fov_x / 2.0f, display_params::fov_x / 2.0f,
                                  display_params::fov_y / 2.0f, display_params::fov_y / 2.0f, rendering_params::near_z,
                                  rendering_params::far_z);
    }

    /* Calculate timewarm transform from projection matrix, view matrix, etc */
    static void calculate_timewarp_transform(Eigen::Matrix4f& transform, const Eigen::Matrix4f& renderProjectionMatrix,
                                             const Eigen::Matrix4f& renderViewMatrix, const Eigen::Matrix4f& newViewMatrix) {
        // Eigen stores matrices internally in column-major order.
        // However, the (i,j) accessors are row-major (i.e, the first argument
        // is which row, and the second argument is which column.)
        Eigen::Matrix4f texCoordProjection;
        texCoordProjection << 0.5f * renderProjectionMatrix(0, 0), 0.0f, 0.5f * renderProjectionMatrix(0, 2) - 0.5f, 0.0f, 0.0f,
            0.5f * renderProjectionMatrix(1, 1), 0.5f * renderProjectionMatrix(1, 2) - 0.5f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f;

        // Calculate the delta between the view matrix used for rendering and
        // a more recent or predicted view matrix based on new sensor input.
        Eigen::Matrix4f inverseRenderViewMatrix = renderViewMatrix.inverse();

        Eigen::Matrix4f deltaViewMatrix = inverseRenderViewMatrix * newViewMatrix;

        deltaViewMatrix(0, 3) = 0.0f;
        deltaViewMatrix(1, 3) = 0.0f;
        deltaViewMatrix(2, 3) = 0.0f;

        // Accumulate the transforms.
        transform = texCoordProjection * deltaViewMatrix;
    }

    const phonebook* const                 pb;
    const std::shared_ptr<switchboard>     sb;
    const std::shared_ptr<pose_prediction> pp;
    bool                                   disable_warp = false;
    std::shared_ptr<display_sink>          ds           = nullptr;
    std::mutex                             m_setup;

    bool initialized                      = false;
    bool input_texture_vulkan_coordinates = true;

    // Vulkan resources
    std::stack<std::function<void()>> deletion_queue;
    VmaAllocator                      vma_allocator{};

    std::array<std::vector<VkImageView>, 2> buffer_pool;
    VkSampler                               fb_sampler{};

    VkDescriptorPool                            descriptor_pool{};
    VkDescriptorSetLayout                       descriptor_set_layout{};
    std::array<std::vector<VkDescriptorSet>, 2> descriptor_sets;

    VkPipelineLayout  pipeline_layout{};
    VkBuffer          uniform_buffer{};
    VmaAllocation     uniform_alloc{};
    VmaAllocationInfo uniform_alloc_info{};

    VkCommandPool   command_pool{};
    VkCommandBuffer command_buffer{};

    VkBuffer vertex_buffer{};
    VkBuffer index_buffer{};

    // distortion data
    HMD::hmd_info_t hmd_info{};

    uint32_t                         num_distortion_vertices{};
    uint32_t                         num_distortion_indices{};
    Eigen::Matrix4f                  basicProjection;
    std::vector<HMD::mesh_coord3d_t> distortion_positions;
    std::vector<HMD::uv_coord_t>     distortion_uv0;
    std::vector<HMD::uv_coord_t>     distortion_uv1;
    std::vector<HMD::uv_coord_t>     distortion_uv2;

    std::vector<uint32_t> distortion_indices;

    // metrics
    std::atomic<uint32_t> num_record_calls{0};
    std::atomic<uint32_t> num_update_uniforms_calls{0};

    friend class timewarp_vk_plugin;
};

class timewarp_vk_plugin : public threadloop {
public:
    timewarp_vk_plugin(const std::string& name, phonebook* pb)
        : threadloop{name, pb}
        , tw{std::make_shared<timewarp_vk>(pb)} {
        pb->register_impl<timewarp>(std::static_pointer_cast<timewarp>(tw));
    }

    void _p_one_iteration() override {
        auto fps = tw->num_record_calls.exchange(0) / 2; // two eyes
        auto ups = tw->num_update_uniforms_calls.exchange(0);

        // std::cout << "timewarp_vk: cb records: " << fps << ", uniform updates: " << ups << std::endl;
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
    std::shared_ptr<timewarp_vk>  tw;
    std::shared_ptr<display_sink> ds;

    int64_t last_print = 0;
};

PLUGIN_MAIN(timewarp_vk_plugin)