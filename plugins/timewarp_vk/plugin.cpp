#define VMA_IMPLEMENTATION

#include "plugin.hpp"

#include "illixr/data_format.hpp"
#include "illixr/global_module_defs.hpp"
#include "illixr/math_util.hpp"
#include "illixr/vk_util/vulkan_utils.hpp"

#include <future>
#include <mutex>
#include <vulkan/vulkan_core.h>

using namespace ILLIXR;

struct vertex {
    glm::vec3 pos;
    glm::vec2 uv0;
    glm::vec2 uv1;
    glm::vec2 uv2;

    static VkVertexInputBindingDescription get_binding_description() {
        VkVertexInputBindingDescription binding_description = {};
        binding_description.binding                         = 0;              // index of the binding in the array of bindings
        binding_description.stride                          = sizeof(vertex); // number of bytes from one entry to the next
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
            offsetof(vertex, pos); // number of bytes since the start of the per-vertex data to read from

        // uv0
        attribute_descriptions[1].binding  = 0;
        attribute_descriptions[1].location = 1;
        attribute_descriptions[1].format   = VK_FORMAT_R32G32_SFLOAT;
        attribute_descriptions[1].offset   = offsetof(vertex, uv0);

        // uv1
        attribute_descriptions[2].binding  = 0;
        attribute_descriptions[2].location = 2;
        attribute_descriptions[2].format   = VK_FORMAT_R32G32_SFLOAT;
        attribute_descriptions[2].offset   = offsetof(vertex, uv1);

        // uv2
        attribute_descriptions[3].binding  = 0;
        attribute_descriptions[3].location = 3;
        attribute_descriptions[3].format   = VK_FORMAT_R32G32_SFLOAT;
        attribute_descriptions[3].offset   = offsetof(vertex, uv2);

        return attribute_descriptions;
    }
};

struct uniform_buffer_object {
    glm::mat4 timewarp_start_transform;
    glm::mat4 timewarp_end_transform;
};

timewarp_vk::timewarp_vk(const phonebook* const pb)
    : phonebook_{pb}
    , switchboard_{phonebook_->lookup_impl<switchboard>()}
    , pose_prediction_{phonebook_->lookup_impl<pose_prediction>()}
    , disable_warp_{ILLIXR::str_to_bool(ILLIXR::getenv_or("ILLIXR_TIMEWARP_DISABLE", "False"))} { }

void timewarp_vk::initialize() {
    display_sink_ = phonebook_->lookup_impl<display_sink>();

    if (display_sink_->vma_allocator) {
        this->vma_allocator_ = display_sink_->vma_allocator;
    } else {
        this->vma_allocator_ = vulkan_utils::create_vma_allocator(display_sink_->vk_instance, display_sink_->vk_physical_device,
                                                                  display_sink_->vk_device);
        deletion_queue_.emplace([=]() {
            vmaDestroyAllocator(vma_allocator_);
        });
    }

    generate_distortion_data();
    command_pool_   = vulkan_utils::create_command_pool(display_sink_->vk_device, display_sink_->graphics_queue_family);
    command_buffer_ = vulkan_utils::create_command_buffer(display_sink_->vk_device, command_pool_);
    deletion_queue_.emplace([=]() {
        vkDestroyCommandPool(display_sink_->vk_device, command_pool_, nullptr);
    });
    create_vertex_buffer();
    create_index_buffer();
    create_descriptor_set_layout();
    create_uniform_buffer();
    create_texture_sampler();
}

void timewarp_vk::setup(VkRenderPass render_pass, uint32_t subpass, std::array<std::vector<VkImageView>, 2> buffer_pool_in,
                        bool input_texture_vulkan_coordinates_in) {
    std::lock_guard<std::mutex> lock{setup_mutex_};

    this->input_texture_vulkan_coordinates_ = input_texture_vulkan_coordinates_in;
    if (!initialized_) {
        initialize();
        initialized_ = true;
    } else {
        partial_destroy();
    }

    if (buffer_pool_in[0].size() != buffer_pool_in[1].size()) {
        throw std::runtime_error("timewarp_vk: buffer_pool_[0].size() != buffer_pool_[1].size()");
    }
    this->buffer_pool_ = buffer_pool_in;

    create_descriptor_pool();
    create_descriptor_sets();
    create_pipeline(render_pass, subpass);
}

void timewarp_vk::partial_destroy() {
    vkDestroyPipeline(display_sink_->vk_device, pipeline, nullptr);
    pipeline = VK_NULL_HANDLE;

    vkDestroyPipelineLayout(display_sink_->vk_device, pipeline_layout_, nullptr);
    pipeline_layout_ = VK_NULL_HANDLE;

    vkDestroyDescriptorPool(display_sink_->vk_device, descriptor_pool_, nullptr);
    descriptor_pool_ = VK_NULL_HANDLE;
}

void timewarp_vk::update_uniforms(const pose_type& render_pose) {
    num_update_uniforms_calls_++;

    // Generate "starting" view matrix, from the pose sampled at the time of rendering the frame
    Eigen::Matrix4f view_matrix   = Eigen::Matrix4f::Identity();
    view_matrix.block(0, 0, 3, 3) = render_pose.orientation.toRotationMatrix();

    // We simulate two asynchronous view matrices, one at the beginning of
    // display refresh, and one at the end of display refresh. The
    // distortion shader will lerp between these two predictive view
    // transformations as it renders across the horizontal view,
    // compensating for display panel refresh delay (wow!)
    Eigen::Matrix4f view_matrix_begin = Eigen::Matrix4f::Identity();
    Eigen::Matrix4f view_matrix_end   = Eigen::Matrix4f::Identity();

    const pose_type latest_pose         = disable_warp_ ? render_pose : pose_prediction_->get_fast_pose().pose;
    view_matrix_begin.block(0, 0, 3, 3) = latest_pose.orientation.toRotationMatrix();

    // TODO: We set the "end" pose to the same as the beginning pose, but this really should be the pose for
    // `display_period` later
    view_matrix_end = view_matrix_begin;

    // Calculate the timewarp transformation matrices. These are a product
    // of the last-known-good view matrix and the predictive transforms.
    Eigen::Matrix4f time_warp_start_transform_4x4;
    Eigen::Matrix4f time_warp_end_transform_4x4;

    // Calculate timewarp transforms using predictive view transforms
    calculate_timewarp_transform(time_warp_start_transform_4x4, basic_projection_, view_matrix, view_matrix_begin);
    calculate_timewarp_transform(time_warp_end_transform_4x4, basic_projection_, view_matrix, view_matrix_end);

    auto* ubo = (uniform_buffer_object*) uniform_alloc_info_.pMappedData;
    memcpy(&ubo->timewarp_start_transform, time_warp_start_transform_4x4.data(), sizeof(glm::mat4));
    memcpy(&ubo->timewarp_end_transform, time_warp_end_transform_4x4.data(), sizeof(glm::mat4));
}

void timewarp_vk::record_command_buffer(VkCommandBuffer command_buffer, int buffer_ind, bool left) {
    num_record_calls_++;

    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(command_buffer, 0, 1, &vertex_buffer_, offsets);
    // for (int eye = 0; eye < HMD::NUM_EYES; eye++) {
    //     vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_, 0, 1,
    //     &descriptor_sets_[eye][buffer_ind], 0, nullptr); vkCmdBindIndexBuffer(command_buffer, index_buffer_, 0,
    //     VK_INDEX_TYPE_UINT32); vkCmdDrawIndexed(command_buffer, num_distortion_indices_, 1, 0, num_distortion_vertices_ *
    //     eye, 0);
    // }
    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_, 0, 1,
                            &descriptor_sets_[!left][buffer_ind], 0, nullptr);
    vkCmdBindIndexBuffer(command_buffer, index_buffer_, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(command_buffer, num_distortion_indices_, 1, 0, static_cast<int>(num_distortion_vertices_ * !left), 0);
}

void timewarp_vk::destroy() {
    partial_destroy();
    // drain deletion_queue_
    while (!deletion_queue_.empty()) {
        deletion_queue_.top()();
        deletion_queue_.pop();
    }
}

void timewarp_vk::create_vertex_buffer() {
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
    staging_buffer_info.size  = sizeof(vertex) * num_distortion_vertices_ * HMD::NUM_EYES;
    staging_buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo staging_alloc_info = {};
    staging_alloc_info.usage                   = VMA_MEMORY_USAGE_AUTO;
    staging_alloc_info.flags                   = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

    VkBuffer      staging_buffer;
    VmaAllocation staging_alloc;
    VK_ASSERT_SUCCESS(
        vmaCreateBuffer(vma_allocator_, &staging_buffer_info, &staging_alloc_info, &staging_buffer, &staging_alloc, nullptr))

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
    buffer_info.size  = sizeof(vertex) * num_distortion_vertices_ * HMD::NUM_EYES;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

    VmaAllocationCreateInfo alloc_info = {};
    alloc_info.usage                   = VMA_MEMORY_USAGE_GPU_ONLY;

    VmaAllocation vertex_alloc;
    VK_ASSERT_SUCCESS(vmaCreateBuffer(vma_allocator_, &buffer_info, &alloc_info, &vertex_buffer_, &vertex_alloc, nullptr))

    std::vector<vertex> vertices;
    vertices.resize(num_distortion_vertices_ * HMD::NUM_EYES);
    for (size_t i = 0; i < num_distortion_vertices_ * HMD::NUM_EYES; i++) {
        vertices[i].pos = {distortion_positions_[i].x, distortion_positions_[i].y, distortion_positions_[i].z};
        vertices[i].uv0 = {distortion_uv0_[i].u, distortion_uv0_[i].v};
        vertices[i].uv1 = {distortion_uv1_[i].u, distortion_uv1_[i].v};
        vertices[i].uv2 = {distortion_uv2_[i].u, distortion_uv2_[i].v};
    }

    void* mapped_data;
    VK_ASSERT_SUCCESS(vmaMapMemory(vma_allocator_, staging_alloc, &mapped_data))
    memcpy(mapped_data, vertices.data(), sizeof(vertex) * num_distortion_vertices_ * HMD::NUM_EYES);
    vmaUnmapMemory(vma_allocator_, staging_alloc);

    VkCommandBuffer command_buffer_local = vulkan_utils::begin_one_time_command(display_sink_->vk_device, command_pool_);
    VkBufferCopy    copy_region          = {};
    copy_region.size                     = sizeof(vertex) * num_distortion_vertices_ * HMD::NUM_EYES;
    vkCmdCopyBuffer(command_buffer_local, staging_buffer, vertex_buffer_, 1, &copy_region);
    vulkan_utils::end_one_time_command(display_sink_->vk_device, command_pool_, display_sink_->graphics_queue,
                                       command_buffer_local);

    vmaDestroyBuffer(vma_allocator_, staging_buffer, staging_alloc);

    deletion_queue_.emplace([=]() {
        vmaDestroyBuffer(vma_allocator_, vertex_buffer_, vertex_alloc);
    });
}

void timewarp_vk::create_index_buffer() {
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
    staging_buffer_info.size  = sizeof(uint32_t) * num_distortion_indices_;
    staging_buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo staging_alloc_info = {};
    staging_alloc_info.usage                   = VMA_MEMORY_USAGE_AUTO;
    staging_alloc_info.flags                   = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

    VkBuffer      staging_buffer;
    VmaAllocation staging_alloc;
    VK_ASSERT_SUCCESS(
        vmaCreateBuffer(vma_allocator_, &staging_buffer_info, &staging_alloc_info, &staging_buffer, &staging_alloc, nullptr))

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
    buffer_info.size  = sizeof(uint32_t) * num_distortion_indices_;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

    VmaAllocationCreateInfo alloc_info = {};
    alloc_info.usage                   = VMA_MEMORY_USAGE_GPU_ONLY;

    VmaAllocation index_alloc;
    VK_ASSERT_SUCCESS(vmaCreateBuffer(vma_allocator_, &buffer_info, &alloc_info, &index_buffer_, &index_alloc, nullptr))

    void* mapped_data;
    VK_ASSERT_SUCCESS(vmaMapMemory(vma_allocator_, staging_alloc, &mapped_data))
    memcpy(mapped_data, distortion_indices_.data(), sizeof(uint32_t) * num_distortion_indices_);
    vmaUnmapMemory(vma_allocator_, staging_alloc);

    VkCommandBuffer command_buffer_local = vulkan_utils::begin_one_time_command(display_sink_->vk_device, command_pool_);
    VkBufferCopy    copy_region          = {};
    copy_region.size                     = sizeof(uint32_t) * num_distortion_indices_;
    vkCmdCopyBuffer(command_buffer_local, staging_buffer, index_buffer_, 1, &copy_region);
    vulkan_utils::end_one_time_command(display_sink_->vk_device, command_pool_, display_sink_->graphics_queue,
                                       command_buffer_local);

    vmaDestroyBuffer(vma_allocator_, staging_buffer, staging_alloc);

    deletion_queue_.emplace([=]() {
        vmaDestroyBuffer(vma_allocator_, index_buffer_, index_alloc);
    });
}

void timewarp_vk::generate_distortion_data() {
    // Generate reference HMD and physical body dimensions
    HMD::get_default_hmd_info(display_params::width_pixels, display_params::height_pixels, display_params::width_meters,
                              display_params::height_meters, display_params::lens_separation,
                              display_params::meters_per_tan_angle, display_params::aberration, hmd_info_);

    // Construct timewarp meshes and other data
    build_timewarp(hmd_info_);
}

void timewarp_vk::create_texture_sampler() {
    VkSamplerCreateInfo sampler_info = {
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
    sampler_info.magFilter = VK_FILTER_LINEAR; // how to interpolate texels that are magnified on screen
    sampler_info.minFilter = VK_FILTER_LINEAR;

    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sampler_info.borderColor  = VK_BORDER_COLOR_INT_OPAQUE_BLACK; // black outside the texture

    sampler_info.anisotropyEnable        = VK_FALSE;
    sampler_info.unnormalizedCoordinates = VK_FALSE;
    sampler_info.compareEnable           = VK_FALSE;
    sampler_info.compareOp               = VK_COMPARE_OP_ALWAYS;

    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_info.mipLodBias = 0.f;
    sampler_info.minLod     = 0.f;
    sampler_info.maxLod     = 0.f;

    VK_ASSERT_SUCCESS(vkCreateSampler(display_sink_->vk_device, &sampler_info, nullptr, &fb_sampler_))

    deletion_queue_.emplace([=]() {
        vkDestroySampler(display_sink_->vk_device, fb_sampler_, nullptr);
    });
}

void timewarp_vk::create_descriptor_set_layout() {
    VkDescriptorSetLayoutBinding ubo_layout_binding = {};
    ubo_layout_binding.binding                      = 0; // binding number in the shader
    ubo_layout_binding.descriptorType               = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ubo_layout_binding.descriptorCount              = 1;
    ubo_layout_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT; // shader stages that can access the descriptor

    VkDescriptorSetLayoutBinding sampler_layout_binding = {};
    sampler_layout_binding.binding                      = 1;
    sampler_layout_binding.descriptorType               = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sampler_layout_binding.descriptorCount              = 1;
    sampler_layout_binding.stageFlags                   = VK_SHADER_STAGE_FRAGMENT_BIT;

    std::array<VkDescriptorSetLayoutBinding, 2> bindings    = {ubo_layout_binding, sampler_layout_binding};
    VkDescriptorSetLayoutCreateInfo             layout_info = {};
    layout_info.sType                                       = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount                                = static_cast<uint32_t>(bindings.size());
    layout_info.pBindings                                   = bindings.data(); // array of VkDescriptorSetLayoutBinding structs

    VK_ASSERT_SUCCESS(vkCreateDescriptorSetLayout(display_sink_->vk_device, &layout_info, nullptr, &descriptor_set_layout_))
    deletion_queue_.emplace([=]() {
        vkDestroyDescriptorSetLayout(display_sink_->vk_device, descriptor_set_layout_, nullptr);
    });
}

void timewarp_vk::create_uniform_buffer() {
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
    buffer_info.size  = sizeof(uniform_buffer_object);
    buffer_info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

    VmaAllocationCreateInfo create_info = {};
    create_info.usage                   = VMA_MEMORY_USAGE_AUTO;
    create_info.flags         = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    create_info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    VK_ASSERT_SUCCESS(
        vmaCreateBuffer(vma_allocator_, &buffer_info, &create_info, &uniform_buffer_, &uniform_alloc_, &uniform_alloc_info_))
    deletion_queue_.emplace([=]() {
        vmaDestroyBuffer(vma_allocator_, uniform_buffer_, uniform_alloc_);
    });
}

void timewarp_vk::create_descriptor_pool() {
    std::array<VkDescriptorPoolSize, 2> pool_sizes = {};
    pool_sizes[0].type                             = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    pool_sizes[0].descriptorCount                  = 2;
    pool_sizes[1].type                             = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_sizes[1].descriptorCount                  = 2;

    VkDescriptorPoolCreateInfo pool_info = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, // sType
        nullptr,                                       // pNext
        0,                                             // flags
        0,                                             // maxSets
        0,                                             // poolSizeCount
        nullptr                                        // pPoolSizes
    };
    pool_info.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
    pool_info.pPoolSizes    = pool_sizes.data();
    pool_info.maxSets       = buffer_pool_[0].size() * 2;

    VK_ASSERT_SUCCESS(vkCreateDescriptorPool(display_sink_->vk_device, &pool_info, nullptr, &descriptor_pool_))
}

void timewarp_vk::create_descriptor_sets() {
    // single frame in flight for now
    for (int eye = 0; eye < 2; eye++) {
        std::vector<VkDescriptorSetLayout> layouts    = {buffer_pool_[0].size(), descriptor_set_layout_};
        VkDescriptorSetAllocateInfo        alloc_info = {
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, // sType
            nullptr,                                        // pNext
            {},                                             // descriptorPool
            0,                                              // descriptorSetCount
            nullptr                                         // pSetLayouts
        };
        alloc_info.descriptorPool     = descriptor_pool_;
        alloc_info.descriptorSetCount = buffer_pool_[0].size();
        alloc_info.pSetLayouts        = layouts.data();

        descriptor_sets_[eye].resize(buffer_pool_[0].size());
        VK_ASSERT_SUCCESS(vkAllocateDescriptorSets(display_sink_->vk_device, &alloc_info, descriptor_sets_[eye].data()))

        for (size_t i = 0; i < buffer_pool_[0].size(); i++) {
            VkDescriptorBufferInfo buffer_info = {};
            buffer_info.buffer                 = uniform_buffer_;
            buffer_info.offset                 = 0;
            buffer_info.range                  = sizeof(uniform_buffer_object);

            VkDescriptorImageInfo image_info = {};
            image_info.imageLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            image_info.imageView             = buffer_pool_[eye][i];
            image_info.sampler               = fb_sampler_;

            std::array<VkWriteDescriptorSet, 2> descriptor_writes = {};

            descriptor_writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptor_writes[0].dstSet          = descriptor_sets_[eye][i];
            descriptor_writes[0].dstBinding      = 0;
            descriptor_writes[0].dstArrayElement = 0;
            descriptor_writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            descriptor_writes[0].descriptorCount = 1;
            descriptor_writes[0].pBufferInfo     = &buffer_info;

            descriptor_writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptor_writes[1].dstSet          = descriptor_sets_[eye][i];
            descriptor_writes[1].dstBinding      = 1;
            descriptor_writes[1].dstArrayElement = 0;
            descriptor_writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptor_writes[1].descriptorCount = 1;
            descriptor_writes[1].pImageInfo      = &image_info;

            vkUpdateDescriptorSets(display_sink_->vk_device, static_cast<uint32_t>(descriptor_writes.size()),
                                   descriptor_writes.data(), 0, nullptr);
        }
    }
}

VkPipeline timewarp_vk::create_pipeline(VkRenderPass render_pass, [[maybe_unused]] uint32_t subpass) {
    if (pipeline != VK_NULL_HANDLE) {
        throw std::runtime_error("timewarp_vk::create_pipeline: pipeline already created");
    }

    VkDevice device = display_sink_->vk_device;

    auto           folder = std::string(SHADER_FOLDER);
    VkShaderModule vert   = vulkan_utils::create_shader_module(device, vulkan_utils::read_file(folder + "/tw.vert.spv"));
    VkShaderModule frag   = vulkan_utils::create_shader_module(device, vulkan_utils::read_file(folder + "/tw.frag.spv"));

    VkPipelineShaderStageCreateInfo vert_stage_info = {};
    vert_stage_info.sType                           = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vert_stage_info.stage                           = VK_SHADER_STAGE_VERTEX_BIT;
    vert_stage_info.module                          = vert;
    vert_stage_info.pName                           = "main";

    VkPipelineShaderStageCreateInfo frag_stage_info = {};
    frag_stage_info.sType                           = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    frag_stage_info.stage                           = VK_SHADER_STAGE_FRAGMENT_BIT;
    frag_stage_info.module                          = frag;
    frag_stage_info.pName                           = "main";

    VkPipelineShaderStageCreateInfo shader_stages[] = {vert_stage_info, frag_stage_info};

    auto binding_description    = vertex::get_binding_description();
    auto attribute_descriptions = vertex::get_attribute_descriptions();

    VkPipelineVertexInputStateCreateInfo vertex_input_info = {};
    vertex_input_info.sType                                = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_info.vertexBindingDescriptionCount        = 1;
    vertex_input_info.pVertexBindingDescriptions           = &binding_description;
    vertex_input_info.vertexAttributeDescriptionCount      = static_cast<uint32_t>(attribute_descriptions.size());
    vertex_input_info.pVertexAttributeDescriptions         = attribute_descriptions.data();

    VkPipelineInputAssemblyStateCreateInfo input_assembly = {};
    input_assembly.sType                                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology                               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

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

    VkPipelineColorBlendAttachmentState color_blend_attachment = {};
    color_blend_attachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    color_blend_attachment.blendEnable = VK_FALSE;

    // disable blending
    VkPipelineColorBlendStateCreateInfo color_blending = {};
    color_blending.sType                               = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.attachmentCount                     = 1;
    color_blending.pAttachments                        = &color_blend_attachment;

    // disable depth testing
    VkPipelineDepthStencilStateCreateInfo depth_stencil = {};
    depth_stencil.sType                                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil.depthTestEnable                       = VK_FALSE;

    // use dynamic state instead
    // VkViewport viewport = {};
    // viewport.x = 0.0f;
    // viewport.y = 0.0f;
    // viewport.width = display_sink_->swapchain_extent.width;
    // viewport.height = display_sink_->swapchain_extent.height;
    // viewport.minDepth = 0.0f;
    // viewport.maxDepth = 1.0f;

    // VkRect2D scissor = {};
    // scissor.offset = {0, 0};
    // scissor.extent = display_sink_->swapchain_extent;

    // VkPipelineViewportStateCreateInfo viewport_state_create_info = {};
    // viewport_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    // viewport_state_create_info.viewportCount = 1;
    // viewport_state_create_info.pViewports = &viewport;
    // viewport_state_create_info.scissorCount = 1;
    // viewport_state_create_info.pScissors = &scissor;

    std::vector<VkDynamicState> dynamic_states = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

    VkPipelineDynamicStateCreateInfo dynamic_state_create_info = {};
    dynamic_state_create_info.sType                            = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state_create_info.dynamicStateCount                = static_cast<uint32_t>(dynamic_states.size());
    dynamic_state_create_info.pDynamicStates                   = dynamic_states.data();

    VkPipelineViewportStateCreateInfo viewport_state_create_info = {};
    viewport_state_create_info.sType                             = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state_create_info.viewportCount                     = 1;
    viewport_state_create_info.scissorCount                      = 1;

    VkPipelineLayoutCreateInfo pipeline_layout_info = {};
    pipeline_layout_info.sType                      = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.setLayoutCount             = 1;
    pipeline_layout_info.pSetLayouts                = &descriptor_set_layout_;

    VK_ASSERT_SUCCESS(vkCreatePipelineLayout(device, &pipeline_layout_info, nullptr, &pipeline_layout_))

    VkGraphicsPipelineCreateInfo pipeline_info = {};
    pipeline_info.sType                        = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount                   = 2;
    pipeline_info.pStages                      = shader_stages;
    pipeline_info.pVertexInputState            = &vertex_input_info;
    pipeline_info.pInputAssemblyState          = &input_assembly;
    pipeline_info.pViewportState               = &viewport_state_create_info;
    pipeline_info.pRasterizationState          = &rasterizer;
    pipeline_info.pMultisampleState            = &multisampling;
    pipeline_info.pColorBlendState             = &color_blending;
    pipeline_info.pDepthStencilState           = nullptr;
    pipeline_info.pDynamicState                = &dynamic_state_create_info;

    pipeline_info.layout     = pipeline_layout_;
    pipeline_info.renderPass = render_pass;
    pipeline_info.subpass    = 0;

    VK_ASSERT_SUCCESS(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline))

    vkDestroyShaderModule(device, vert, nullptr);
    vkDestroyShaderModule(device, frag, nullptr);
    return pipeline;
}

void timewarp_vk::build_timewarp(HMD::hmd_info_t& hmd_info) {
    // Calculate the number of vertices+indices in the distortion mesh.
    num_distortion_vertices_ = (hmd_info.eye_tiles_high + 1) * (hmd_info.eye_tiles_wide + 1);
    num_distortion_indices_  = hmd_info.eye_tiles_high * hmd_info.eye_tiles_wide * 6;

    // Allocate memory for the elements/indices array.
    distortion_indices_.resize(num_distortion_indices_);

    // This is just a simple grid/plane index array, nothing fancy.
    // Same for both eye distortions, too!
    for (int y = 0; y < hmd_info.eye_tiles_high; y++) {
        for (int x = 0; x < hmd_info.eye_tiles_wide; x++) {
            const int offset = (y * hmd_info.eye_tiles_wide + x) * 6;

            distortion_indices_[offset + 0] = ((y + 0) * (hmd_info.eye_tiles_wide + 1) + (x + 0));
            distortion_indices_[offset + 1] = ((y + 1) * (hmd_info.eye_tiles_wide + 1) + (x + 0));
            distortion_indices_[offset + 2] = ((y + 0) * (hmd_info.eye_tiles_wide + 1) + (x + 1));

            distortion_indices_[offset + 3] = ((y + 0) * (hmd_info.eye_tiles_wide + 1) + (x + 1));
            distortion_indices_[offset + 4] = ((y + 1) * (hmd_info.eye_tiles_wide + 1) + (x + 0));
            distortion_indices_[offset + 5] = ((y + 1) * (hmd_info.eye_tiles_wide + 1) + (x + 1));
        }
    }

    // There are `num_distortion_vertices_` distortion coordinates for each color channel (3) of each eye (2).
    // These are NOT the coordinates of the distorted vertices. They are *coefficients* that will be used to
    // offset the UV coordinates of the distortion mesh.
    std::array<std::array<std::vector<HMD::mesh_coord2d_t>, HMD::NUM_COLOR_CHANNELS>, HMD::NUM_EYES> distort_coords;
    for (auto& eye_coords : distort_coords) {
        for (auto& channel_coords : eye_coords) {
            channel_coords.resize(num_distortion_vertices_);
        }
    }
    HMD::build_distortion_meshes(distort_coords, hmd_info);

    // Allocate memory for position and UV CPU buffers.
    const std::size_t num_elems_pos_uv = HMD::NUM_EYES * num_distortion_vertices_;
    distortion_positions_.resize(num_elems_pos_uv);
    distortion_uv0_.resize(num_elems_pos_uv);
    distortion_uv1_.resize(num_elems_pos_uv);
    distortion_uv2_.resize(num_elems_pos_uv);

    for (int eye = 0; eye < HMD::NUM_EYES; eye++) {
        for (int y = 0; y <= hmd_info.eye_tiles_high; y++) {
            for (int x = 0; x <= hmd_info.eye_tiles_wide; x++) {
                const int index = y * (hmd_info.eye_tiles_wide + 1) + x;

                // Set the physical distortion mesh coordinates. These are rectangular/grid-like, not distorted.
                // The distortion is handled by the UVs, not the actual mesh coordinates!
                // distortion_positions_[eye * num_distortion_vertices_ + index].x = (-1.0f + eye + ((float) x /
                // hmd_info.eyeTilesWide));
                distortion_positions_[eye * num_distortion_vertices_ + index].x =
                    (-1.0f + (static_cast<float>(x) / static_cast<float>(hmd_info.eye_tiles_wide)));

                // flip the y coordinates for Vulkan texture
                distortion_positions_[eye * num_distortion_vertices_ + index].y =
                    (input_texture_vulkan_coordinates_ ? -1.0f : 1.0f) *
                    (-1.0f +
                     2.0f * (static_cast<float>(hmd_info.eye_tiles_high - y) / static_cast<float>(hmd_info.eye_tiles_high)) *
                         (static_cast<float>(hmd_info.eye_tiles_high * hmd_info.tile_pixels_high) /
                          static_cast<float>(hmd_info.display_pixels_high)));
                distortion_positions_[eye * num_distortion_vertices_ + index].z = 0.0f;

                // Use the previously-calculated distort_coords to set the UVs on the distortion mesh
                distortion_uv0_[eye * num_distortion_vertices_ + index].u = distort_coords[eye][0][index].x;
                distortion_uv0_[eye * num_distortion_vertices_ + index].v = distort_coords[eye][0][index].y;
                distortion_uv1_[eye * num_distortion_vertices_ + index].u = distort_coords[eye][1][index].x;
                distortion_uv1_[eye * num_distortion_vertices_ + index].v = distort_coords[eye][1][index].y;
                distortion_uv2_[eye * num_distortion_vertices_ + index].u = distort_coords[eye][2][index].x;
                distortion_uv2_[eye * num_distortion_vertices_ + index].v = distort_coords[eye][2][index].y;
            }
        }
    }

    // Construct perspective projection matrix
    math_util::projection_fov(&basic_projection_, display_params::fov_x / 2.0f, display_params::fov_x / 2.0f,
                              display_params::fov_y / 2.0f, display_params::fov_y / 2.0f, rendering_params::near_z,
                              rendering_params::far_z);
}

/* Calculate timewarp transform from projection matrix, view matrix, etc */
void timewarp_vk::calculate_timewarp_transform(Eigen::Matrix4f& transform, const Eigen::Matrix4f& render_projection_matrix,
                                               const Eigen::Matrix4f& render_view_matrix,
                                               const Eigen::Matrix4f& new_view_matrix) {
    // Eigen stores matrices internally in column-major order.
    // However, the (i,j) accessors are row-major (i.e, the first argument
    // is which row, and the second argument is which column.)
    Eigen::Matrix4f tex_coord_projection;
    tex_coord_projection << 0.5f * render_projection_matrix(0, 0), 0.0f, 0.5f * render_projection_matrix(0, 2) - 0.5f, 0.0f,
        0.0f, 0.5f * render_projection_matrix(1, 1), 0.5f * render_projection_matrix(1, 2) - 0.5f, 0.0f, 0.0f, 0.0f, -1.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 1.0f;

    // Calculate the delta between the view matrix used for rendering and
    // a more recent or predicted view matrix based on new sensor input.
    Eigen::Matrix4f inverse_render_view_matrix = render_view_matrix.inverse();

    Eigen::Matrix4f delta_view_matrix = inverse_render_view_matrix * new_view_matrix;

    delta_view_matrix(0, 3) = 0.0f;
    delta_view_matrix(1, 3) = 0.0f;
    delta_view_matrix(2, 3) = 0.0f;

    // Accumulate the transforms.
    transform = tex_coord_projection * delta_view_matrix;
}

[[maybe_unused]] timewarp_vk_plugin::timewarp_vk_plugin(const std::string& name, phonebook* pb)
    : threadloop{name, pb}
    , timewarp_{std::make_shared<timewarp_vk>(phonebook_)} {
    pb->register_impl<timewarp>(std::static_pointer_cast<timewarp>(timewarp_));
}

void timewarp_vk_plugin::_p_one_iteration() {
    // auto fps = timewarp_->num_record_calls_.exchange(0) / 2; // two eyes
    // auto ups = timewarp_->num_update_uniforms_calls_.exchange(0);

    // std::cout << "timewarp_vk: cb records: " << fps << ", uniform updates: " << ups << std::endl;
}

threadloop::skip_option timewarp_vk_plugin::_p_should_skip() {
    // Get the current time in milliseconds
    auto now =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    // Only print every 1 second
    if (now - last_print_ < 1000) {
        return skip_option::skip_and_yield;
    } else {
        last_print_ = now;
        return skip_option::run;
    }
}

PLUGIN_MAIN(timewarp_vk_plugin)