#include "service.hpp"

#include "illixr/global_module_defs.hpp"
#include "illixr/math_util.hpp"

#include <array>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <vulkan/vulkan_core.h>

#define TINYOBJLOADER_IMPLEMENTATION
#include "illixr/gl_util/lib/tiny_obj_loader.h"

#define STB_IMAGE_IMPLEMENTATION
#include "illixr/gl_util/lib/stb_image.h"
#include "illixr/vk/display_provider.hpp"
#include "illixr/vk/render_pass.hpp"
#include "illixr/vk/vulkan_utils.hpp"

#include <unordered_map>

using namespace ILLIXR;
using namespace ILLIXR::data_format;

struct model_push_constant {
    [[maybe_unused]] int texture_index;
};

namespace std {
template<>
struct hash<vertex> {
    size_t operator()(vertex const& vertex) const {
        return ((hash<glm::vec3>()(vertex.pos) ^ (hash<glm::vec2>()(vertex.uv) << 1)) >> 1);
    }
};
} // namespace std

struct uniform_buffer_object {
    glm::mat4 model_view;
    glm::mat4 proj;
};

vkdemo::vkdemo(const phonebook* const pb)
    : switchboard_{pb->lookup_impl<switchboard>()}
    , display_provider_{pb->lookup_impl<vulkan::display_provider>()}
    , clock_{pb->lookup_impl<relative_clock>()} { }

void vkdemo::initialize() {
    if (display_provider_->vma_allocator_) {
        this->vma_allocator_ = display_provider_->vma_allocator_;
    } else {
        this->vma_allocator_ = vulkan::create_vma_allocator(
            display_provider_->vk_instance_, display_provider_->vk_physical_device_, display_provider_->vk_device_);

        deletion_queue_.emplace([=]() {
            vmaDestroyAllocator(vma_allocator_);
        });
    }

    command_pool_ =
        vulkan::create_command_pool(display_provider_->vk_device_, display_provider_->queues_[vulkan::queue::GRAPHICS].family);
    command_buffer_ = vulkan::create_command_buffer(display_provider_->vk_device_, command_pool_);
    deletion_queue_.emplace([=]() {
        vkDestroyCommandPool(display_provider_->vk_device_, command_pool_, nullptr);
    });

    load_model();
    bake_models();
    create_texture_sampler_();
    create_descriptor_set_layout();
    create_uniform_buffers();
    create_descriptor_pool();
    create_descriptor_set();
    create_vertex_buffer();
    create_index_buffer();

    vertices_.clear();
    indices_.clear();

    // Construct perspective projection matrix (using the same projection matrix as Unreal Engine, by default).
    for (int eye = 0; eye < 2; eye++) {
        math_util::unreal_projection(&basic_projection_[eye], index_params::fov_left[eye], index_params::fov_right[eye],
                                     index_params::fov_up[eye], index_params::fov_down[eye]);
    }
}

void vkdemo::setup(VkRenderPass render_pass, uint32_t subpass, std::shared_ptr<vulkan::buffer_pool<fast_pose_type>> _) {
    create_pipeline(render_pass, subpass);
}

void vkdemo::update_uniforms(const pose_type& fp) {
    update_uniform(fp, 0);
    update_uniform(fp, 1);
}

void vkdemo::record_command_buffer(VkCommandBuffer command_buffer, VkFramebuffer frame_buffer, int buffer_ind, bool left) {
    (void) frame_buffer;
    (void) buffer_ind;
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    VkBuffer     vertexBuffers[] = {vertex_buffer_};
    VkDeviceSize offsets[]       = {0};
    vkCmdBindVertexBuffers(command_buffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(command_buffer, index_buffer_, 0, VK_INDEX_TYPE_UINT32);
    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_, 0, 1, &descriptor_sets_[!left],
                            0, nullptr);

    for (auto& model : models_) {
        model_push_constant push_constant{};
        push_constant.texture_index = static_cast<int>(texture_map_[model.texture_index]);
        vkCmdPushConstants(command_buffer, pipeline_layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(model_push_constant),
                           &push_constant);
        vkCmdDrawIndexed(command_buffer, model.index_count, 1, model.index_offset, 0, 0);
    }
}

void vkdemo::destroy() {
    vkDeviceWaitIdle(display_provider_->vk_device_);

    // drain deletion_queue_
    while (!deletion_queue_.empty()) {
        deletion_queue_.top()();
        deletion_queue_.pop();
    }
}

void vkdemo::update_uniform(const pose_type& pose, int eye) {
    Eigen::Matrix4f model_matrix = Eigen::Matrix4f::Identity();

    Eigen::Matrix3f head_rotation_matrix = pose.orientation.toRotationMatrix();

    constexpr int LEFT_EYE = 0;

    // Offset of eyeball from pose
    auto eyeball = Eigen::Vector3f((eye == LEFT_EYE ? -display_params::ipd / 2.0f : display_params::ipd / 2.0f), 0, 0);

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

    Eigen::Matrix4f model_view = view_matrix * model_matrix;

    auto* ubo = (uniform_buffer_object*) uniform_buffer_allocation_infos_[eye].pMappedData;
    memcpy(&ubo->model_view, &model_view, sizeof(model_view));
    memcpy(&ubo->proj, &basic_projection_[eye], sizeof(basic_projection_[eye]));
}

void vkdemo::bake_models() {
    for (std::size_t i = 0; i < textures_.size(); i++) {
        if (textures_[i].image_view == VK_NULL_HANDLE) {
            continue;
        }
        VkDescriptorImageInfo image_info{
            nullptr,                                  // sampler
            textures_[i].image_view,                  // imageView
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, // imageLayout
        };
        texture_map_.insert(std::make_pair(i, image_infos_.size()));
        image_infos_.push_back(image_info);
    }
}

void vkdemo::create_descriptor_set_layout() {
    VkDescriptorSetLayoutBinding ubo_layout_binding{
        0,                                 // binding
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, // descriptorType
        1,                                 // descriptorCount
        VK_SHADER_STAGE_VERTEX_BIT,        // stageFlags
        nullptr                            // pImmutableSamplers
    };

    VkDescriptorSetLayoutBinding sampler_layout_binding{
        1,                            // binding
        VK_DESCRIPTOR_TYPE_SAMPLER,   // descriptorType
        1,                            // descriptorCount
        VK_SHADER_STAGE_FRAGMENT_BIT, // stageFlags
        nullptr                       // pImmutableSamplers
    };

    VkDescriptorSetLayoutBinding sampled_image_layout_binding{
        2,                                      // binding
        VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,       // descriptorType
        static_cast<unsigned int>(texture_map_.size()), // descriptorCount
        VK_SHADER_STAGE_FRAGMENT_BIT,           // stageFlags
        nullptr                                 // pImmutableSamplers
    };

    VkDescriptorSetLayoutCreateInfo layout_info{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, // sType
        nullptr,                                             // pNext
        0,                                                   // flags
        3,                                                   // bindingCount
        nullptr                                              // pBindings
    };
    VkDescriptorSetLayoutBinding bindings[]{ubo_layout_binding, sampler_layout_binding, sampled_image_layout_binding};
    layout_info.pBindings = bindings;

    VK_ASSERT_SUCCESS(
        vkCreateDescriptorSetLayout(display_provider_->vk_device_, &layout_info, nullptr, &descriptor_set_layout_))

    deletion_queue_.emplace([=]() {
        vkDestroyDescriptorSetLayout(display_provider_->vk_device_, descriptor_set_layout_, nullptr);
    });
}

void vkdemo::create_uniform_buffers() {
    for (auto i = 0; i < 2; i++) {
        VkBufferCreateInfo buffer_info{
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, // sType
            nullptr,                              // pNext
            0,                                    // flags
            sizeof(uniform_buffer_object),        // size
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,   // usage
            {},                                   // sharingMode
            0,                                    // queueFamilyIndexCount
            nullptr                               // pQueueFamilyIndices
        };

        VmaAllocationCreateInfo alloc_info{};
        alloc_info.flags         = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
        alloc_info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

        VK_ASSERT_SUCCESS(vmaCreateBuffer(vma_allocator_, &buffer_info, &alloc_info, &uniform_buffers_[i],
                                          &uniform_buffer_allocations_[i], &uniform_buffer_allocation_infos_[i]))

        deletion_queue_.emplace([=]() {
            vmaDestroyBuffer(vma_allocator_, uniform_buffers_[i], uniform_buffer_allocations_[i]);
        });
    }
}

void vkdemo::create_descriptor_pool() {
    std::array<VkDescriptorPoolSize, 3> pool_sizes{{{
                                                        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, // type
                                                        2                                  // descriptorCount
                                                    },
                                                    {
                                                        VK_DESCRIPTOR_TYPE_SAMPLER, // type
                                                        2                           // descriptorCount
                                                    },
                                                    {
                                                        VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,              // type
                                                        static_cast<uint32_t>(2 * texture_map_.size()) // descriptorCount
                                                    }}};
    VkDescriptorPoolCreateInfo          pool_info{
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, // sType
        nullptr,                                       // pNext
        0,                                             // flags
        2,                                             // maxSets
        static_cast<uint32_t>(pool_sizes.size()),      // poolSizeCount
        pool_sizes.data()                              // pPoolSizes
    };

    VK_ASSERT_SUCCESS(vkCreateDescriptorPool(display_provider_->vk_device_, &pool_info, nullptr, &descriptor_pool_))
    deletion_queue_.emplace([=]() {
        vkDestroyDescriptorPool(display_provider_->vk_device_, descriptor_pool_, nullptr);
    });
}

void vkdemo::create_texture_sampler_() {
    VkSamplerCreateInfo sampler_info{
        VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, // sType
        nullptr,                               // pNext
        0,                                     // flags
        VK_FILTER_LINEAR,                      // magFilter
        VK_FILTER_LINEAR,                      // minFilter
        VK_SAMPLER_MIPMAP_MODE_LINEAR,         // mipmapMode
        VK_SAMPLER_ADDRESS_MODE_REPEAT,        // addressModeU
        VK_SAMPLER_ADDRESS_MODE_REPEAT,        // addressModeV
        VK_SAMPLER_ADDRESS_MODE_REPEAT,        // addressModeW
        0.f,                                   // mipLodBias
        VK_FALSE,                              // anisotropyEnable
        // .maxAnisotropy = 16;
        0.f,                              // maxAnisotropy
        VK_FALSE,                         // compareEnable
        VK_COMPARE_OP_ALWAYS,             // compareOp
        0.f,                              // minLod
        0.f,                              // maxLod
        VK_BORDER_COLOR_INT_OPAQUE_BLACK, // borderColor
        VK_FALSE                          // unnormalizedCoordinates
    };

    VK_ASSERT_SUCCESS(vkCreateSampler(display_provider_->vk_device_, &sampler_info, nullptr, &texture_sampler_))
    deletion_queue_.emplace([=]() {
        vkDestroySampler(display_provider_->vk_device_, texture_sampler_, nullptr);
    });
}

void vkdemo::create_descriptor_set() {
    VkDescriptorSetLayout       layouts[] = {descriptor_set_layout_, descriptor_set_layout_};
    VkDescriptorSetAllocateInfo alloc_info{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, // sType
        nullptr,                                        // pNext
        descriptor_pool_,                               // descriptorPool
        2,                                              // descriptorSetCount
        layouts                                         // pSetLayouts
    };

    VK_ASSERT_SUCCESS(vkAllocateDescriptorSets(display_provider_->vk_device_, &alloc_info, descriptor_sets_.data()))

    std::array<VkDescriptorBufferInfo, 2> buffer_infos = {{{
                                                               uniform_buffers_[0],          // buffer
                                                               0,                            // offset
                                                               sizeof(uniform_buffer_object) // range
                                                           },
                                                           {
                                                               uniform_buffers_[1],          // buffer
                                                               0,                            // offset
                                                               sizeof(uniform_buffer_object) // range
                                                           }}};

    std::array<VkWriteDescriptorSet, 2> descriptor_writes = {{{
                                                                  VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, // sType
                                                                  nullptr,                                // pNext
                                                                  descriptor_sets_[0],                    // dstSet
                                                                  0,                                      // dstBinding
                                                                  0,                                      // dstArrayElement
                                                                  1,                                      // descriptorCount
                                                                  VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,      // descriptorType
                                                                  nullptr,                                // pImageInfo
                                                                  &buffer_infos[0],                       // pBufferInfo
                                                                  nullptr                                 // pTexelBufferView
                                                              },
                                                              {
                                                                  VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, // sType
                                                                  nullptr,                                // pNext
                                                                  descriptor_sets_[1],                    // dstSet
                                                                  0,                                      // dstBinding
                                                                  0,                                      // dstArrayElement
                                                                  1,                                      // descriptorCount
                                                                  VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,      // descriptorType
                                                                  nullptr,                                // pImageInfo
                                                                  &buffer_infos[1],                       // pBufferInfo
                                                                  nullptr                                 // pTexelBufferView
                                                              }}};

    vkUpdateDescriptorSets(display_provider_->vk_device_, static_cast<uint32_t>(descriptor_writes.size()),
                           descriptor_writes.data(), 0, nullptr);

    std::vector<VkWriteDescriptorSet> image_descriptor_writes = {};
    for (auto i = 0; i < 2; i++) {
        VkDescriptorImageInfo image_info = {texture_sampler_, nullptr, {}};
        image_descriptor_writes.push_back({
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, // sType
            nullptr,                                // pNext
            descriptor_sets_[i],                    // dstSet
            1,                                      // dstBinding
            0,                                      // dstArrayElement
            1,                                      // descriptorCount
            VK_DESCRIPTOR_TYPE_SAMPLER,             // descriptorType
            &image_info,                            // pImageInfo
            nullptr,                                // pBufferInfo
            nullptr                                 // pTexelBufferView
        });

        assert(!image_infos_.empty());
        image_descriptor_writes.push_back({
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,     // sType
            nullptr,                                    // pNext
            descriptor_sets_[i],                        // dstSet
            2,                                          // dstBinding
            0,                                          // dstArrayElement
            static_cast<uint32_t>(image_infos_.size()), // descriptorCount
            VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,           // descriptorType
            image_infos_.data(),                        // pImageInfo
            nullptr,                                    // pBufferInfo
            nullptr                                     // pTexelBufferView
        });
    }

    vkUpdateDescriptorSets(display_provider_->vk_device_, static_cast<uint32_t>(image_descriptor_writes.size()),
                           image_descriptor_writes.data(), 0, nullptr);
}

void vkdemo::load_texture(const std::string& path, int i) {
    int  width, height, channels;
    auto data = stbi_load(path.c_str(), &width, &height, &channels, 0);
#ifndef NDEBUG
    spdlog::get("illixr")->debug("[vkdemo] Loaded texture {} with dimensions {}x{} and {} channels", path, width, height,
                                 channels);
#endif
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
        data     = new_data;
        channels = 4;
    }

    VkDeviceSize image_size = width * height * channels;

    VkBuffer          staging_buffer;
    VmaAllocation     staging_buffer_allocation;
    VmaAllocationInfo staging_buffer_allocation_info;

    VkBufferCreateInfo buffer_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, // sType
        nullptr,                              // pNext
        0,                                    // flags
        image_size,                           // size
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,     // usage
        {},                                   // sharingMode
        0,                                    // queueFamilyIndexCount
        nullptr                               // pQueueFamilyIndices
    };

    VmaAllocationCreateInfo alloc_info{};
    alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO;

    VK_ASSERT_SUCCESS(vmaCreateBuffer(vma_allocator_, &buffer_info, &alloc_info, &staging_buffer, &staging_buffer_allocation,
                                      &staging_buffer_allocation_info))

    memcpy(staging_buffer_allocation_info.pMappedData, data, static_cast<size_t>(image_size));

    stbi_image_free(data);

    VkImageCreateInfo image_info{
        VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, // sType
        nullptr,                             // pNext
        0,                                   // flags
        VK_IMAGE_TYPE_2D,                    // imageType
        VK_FORMAT_R8G8B8A8_SRGB,             // format
        {
            static_cast<uint32_t>(width),                             // width
            static_cast<uint32_t>(height),                            // height
            1,                                                        // depth
        },                                                            // extent
        1,                                                            // mipLevels
        1,                                                            // arrayLayers
        VK_SAMPLE_COUNT_1_BIT,                                        // samples
        VK_IMAGE_TILING_OPTIMAL,                                      // tiling
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, // usage
        VK_SHARING_MODE_EXCLUSIVE,                                    // sharingMode
        0,                                                            // queueFamilyIndexCount
        nullptr,                                                      // pQueueFamilyIndices
        VK_IMAGE_LAYOUT_UNDEFINED                                     // initialLayout
    };

    VmaAllocationCreateInfo image_alloc_info{};
    image_alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    VK_ASSERT_SUCCESS(vmaCreateImage(vma_allocator_, &image_info, &image_alloc_info, &textures_[i].image,
                                     &textures_[i].image_memory, nullptr))

    image_layout_transition(textures_[i].image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    vulkan::copy_buffer_to_image(display_provider_->vk_device_, display_provider_->queues_[vulkan::queue::GRAPHICS],
                                 command_pool_, staging_buffer, textures_[i].image, width, height);

    image_layout_transition(textures_[i].image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    vmaDestroyBuffer(vma_allocator_, staging_buffer, staging_buffer_allocation);

    deletion_queue_.emplace([=]() {
        vmaDestroyImage(vma_allocator_, textures_[i].image, textures_[i].image_memory);
    });

    VkImageViewCreateInfo view_info{
        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, // sType
        nullptr,                                  // pNext
        0,                                        // flags
        textures_[i].image,                       // image
        VK_IMAGE_VIEW_TYPE_2D,                    // viewType
        VK_FORMAT_R8G8B8A8_SRGB,                  // format
        {},                                       // components
        {
            VK_IMAGE_ASPECT_COLOR_BIT, // aspectMask
            0,                         // baseMipLevel
            1,                         // levelCount
            0,                         // baseArrayLayer
            1                          // layerCount
        } // subresourceRange
    };

    VK_ASSERT_SUCCESS(vkCreateImageView(display_provider_->vk_device_, &view_info, nullptr, &textures_[i].image_view))
    deletion_queue_.emplace([=]() {
        vkDestroyImageView(display_provider_->vk_device_, textures_[i].image_view, nullptr);
    });
}

void vkdemo::image_layout_transition(VkImage image, [[maybe_unused]] VkFormat format, VkImageLayout old_layout,
                                     VkImageLayout new_layout) {
    VkCommandBuffer command_buffer_local = vulkan::begin_one_time_command(display_provider_->vk_device_, command_pool_);

    VkImageMemoryBarrier barrier{
        VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, // sType
        nullptr,                                // pNext
        {},                                     // srcAccessMask
        {},                                     // dstAccessMask
        old_layout,                             // oldLayout
        new_layout,                             // newLayout
        VK_QUEUE_FAMILY_IGNORED,                // srcQueueFamilyIndex
        VK_QUEUE_FAMILY_IGNORED,                // dstQueueFamilyIndex
        image,                                  // image
        {
            (new_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) ? static_cast<VkImageAspectFlags>(VK_IMAGE_ASPECT_DEPTH_BIT)
                                                                             : static_cast<VkImageAspectFlags>(VK_IMAGE_ASPECT_COLOR_BIT), // aspectMask
            0,                                                                                            // baseMipLevel
            1,                                                                                            // levelCount
            0,                                                                                            // baseArrayLayer
            1                                                                                             // layerCount
        } // subresourceRange
    };

    VkPipelineStageFlags source_stage;
    VkPipelineStageFlags destination_stage;

    if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        source_stage      = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destination_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        source_stage      = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destination_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        source_stage      = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destination_stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    } else {
        throw std::invalid_argument("Unsupported layout transition!");
    }

    vkCmdPipelineBarrier(command_buffer_local, source_stage, destination_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    vulkan::end_one_time_command(display_provider_->vk_device_, command_pool_,
                                 display_provider_->queues_[vulkan::queue::GRAPHICS], command_buffer_local);
}

void vkdemo::load_model() {
    tinyobj::attrib_t                attrib;
    std::vector<tinyobj::shape_t>    shapes;
    std::vector<tinyobj::material_t> materials;
    std::string                      warn, err;

    auto path = switchboard_->get_env("ILLIXR_DEMO_DATA") + "/scene.obj";
    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.c_str(),
                          switchboard_->get_env_char("ILLIXR_DEMO_DATA"))) {
        throw std::runtime_error(warn + err);
    }

    textures_.resize(materials.size());

    for (auto i = 0; i < static_cast<int>(materials.size()); i++) {
        auto material = materials[i];
        if (!material.diffuse_texname.empty()) {
            path = switchboard_->get_env("ILLIXR_DEMO_DATA") + "/" + material.diffuse_texname;
            load_texture(path, i);
        }
    }

#ifndef NDEBUG
    spdlog::get("illixr")->debug("[vkdemo] Loaded {} textures_", textures_.size());
#endif

    std::unordered_map<vertex, uint32_t> unique_vertices{};
    for (const auto& shape : shapes) {
        model model{};
        model.index_offset = static_cast<uint32_t>(indices_.size());
        for (const auto& index : shape.mesh.indices) {
            vertex vertex{};

            vertex.pos = {attrib.vertices[3 * index.vertex_index + 0] * 2, attrib.vertices[3 * index.vertex_index + 1] * 2,
                          attrib.vertices[3 * index.vertex_index + 2] * 2};

            vertex.uv = {attrib.texcoords[2 * index.texcoord_index + 0], 1.0f - attrib.texcoords[2 * index.texcoord_index + 1]};

            if (unique_vertices.count(vertex) == 0) {
                unique_vertices[vertex] = static_cast<uint32_t>(vertices_.size());
                vertices_.push_back(vertex);
            }

            indices_.push_back(unique_vertices[vertex]);
        }
        if (!shape.mesh.material_ids.empty()) {
            model.texture_index = shape.mesh.material_ids[0];
        } else {
            model.texture_index = -1;
        }
        model.index_count = static_cast<uint32_t>(shape.mesh.indices.size());
        models_.push_back(model);
    }
}

void vkdemo::create_vertex_buffer() {
    VkBufferCreateInfo staging_buffer_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,    // sType
        nullptr,                                 // pNext
        0,                                       // flags
        sizeof(vertices_[0]) * vertices_.size(), // size
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,        // usage
        {},                                      // sharingMode
        0,                                       // queueFamilyIndexCount
        nullptr                                  // pQueueFamilyIndices
    };

    VmaAllocationCreateInfo staging_alloc_info{};
    staging_alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    staging_alloc_info.usage = VMA_MEMORY_USAGE_AUTO;

    VkBuffer      staging_buffer;
    VmaAllocation staging_buffer_allocation;
    VK_ASSERT_SUCCESS(vmaCreateBuffer(vma_allocator_, &staging_buffer_info, &staging_alloc_info, &staging_buffer,
                                      &staging_buffer_allocation, nullptr))

    VkBufferCreateInfo buffer_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,                                 // sType
        nullptr,                                                              // pNext
        0,                                                                    // flags
        sizeof(vertices_[0]) * vertices_.size(),                              // size
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, // usage
        {},                                                                   // sharingMode
        0,                                                                    // queueFamilyIndexCount
        nullptr                                                               // pQueueFamilyIndices
    };

    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    VmaAllocation buffer_allocation;
    VK_ASSERT_SUCCESS(vmaCreateBuffer(vma_allocator_, &buffer_info, &alloc_info, &vertex_buffer_, &buffer_allocation, nullptr))

    void* mapped_data;
    VK_ASSERT_SUCCESS(vmaMapMemory(vma_allocator_, staging_buffer_allocation, &mapped_data))
    memcpy(mapped_data, vertices_.data(), sizeof(vertices_[0]) * vertices_.size());
    vmaUnmapMemory(vma_allocator_, staging_buffer_allocation);

    VkCommandBuffer command_buffer_local = vulkan::begin_one_time_command(display_provider_->vk_device_, command_pool_);
    VkBufferCopy    copy_region{
        0,                                      // srcOffset
        0,                                      // dstOffset
        sizeof(vertices_[0]) * vertices_.size() // size
    };
    vkCmdCopyBuffer(command_buffer_local, staging_buffer, vertex_buffer_, 1, &copy_region);
    vulkan::end_one_time_command(display_provider_->vk_device_, command_pool_,
                                 display_provider_->queues_[vulkan::queue::GRAPHICS], command_buffer_local);

    vmaDestroyBuffer(vma_allocator_, staging_buffer, staging_buffer_allocation);

    deletion_queue_.emplace([=]() {
        vmaDestroyBuffer(vma_allocator_, vertex_buffer_, buffer_allocation);
    });
}

void vkdemo::create_index_buffer() {
    VkBufferCreateInfo staging_buffer_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,  // sType
        nullptr,                               // pNext
        0,                                     // flags
        sizeof(indices_[0]) * indices_.size(), // size
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,      // usage
        {},                                    // sharingMode
        0,                                     // queueFamilyIndexCount
        nullptr                                // pQueueFamilyIndices
    };

    VmaAllocationCreateInfo staging_alloc_info{};
    staging_alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    staging_alloc_info.usage = VMA_MEMORY_USAGE_AUTO;

    VkBuffer      staging_buffer;
    VmaAllocation staging_buffer_allocation;
    VK_ASSERT_SUCCESS(vmaCreateBuffer(vma_allocator_, &staging_buffer_info, &staging_alloc_info, &staging_buffer,
                                      &staging_buffer_allocation, nullptr))

    VkBufferCreateInfo buffer_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,                                // sType
        nullptr,                                                             // pNext
        0,                                                                   // flags
        sizeof(indices_[0]) * indices_.size(),                               // size
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, // usage
        {},                                                                  // sharingMode
        0,                                                                   // queueFamilyIndexCount
        nullptr                                                              // pQueueFamilyIndices
    };

    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    VmaAllocation buffer_allocation;
    VK_ASSERT_SUCCESS(vmaCreateBuffer(vma_allocator_, &buffer_info, &alloc_info, &index_buffer_, &buffer_allocation, nullptr))

    void* mapped_data;
    VK_ASSERT_SUCCESS(vmaMapMemory(vma_allocator_, staging_buffer_allocation, &mapped_data))
    memcpy(mapped_data, indices_.data(), sizeof(indices_[0]) * indices_.size());
    vmaUnmapMemory(vma_allocator_, staging_buffer_allocation);

    VkCommandBuffer command_buffer_local = vulkan::begin_one_time_command(display_provider_->vk_device_, command_pool_);
    VkBufferCopy    copy_region{
        0,                                    // srcOffset
        0,                                    // dstOffset
        sizeof(indices_[0]) * indices_.size() // size
    };
    vkCmdCopyBuffer(command_buffer_local, staging_buffer, index_buffer_, 1, &copy_region);
    vulkan::end_one_time_command(display_provider_->vk_device_, command_pool_,
                                 display_provider_->queues_[vulkan::queue::GRAPHICS], command_buffer_local);

    vmaDestroyBuffer(vma_allocator_, staging_buffer, staging_buffer_allocation);

    deletion_queue_.emplace([=]() {
        vmaDestroyBuffer(vma_allocator_, index_buffer_, buffer_allocation);
    });
}

void vkdemo::create_pipeline(VkRenderPass render_pass, uint32_t subpass) {
    if (pipeline_ != VK_NULL_HANDLE) {
        throw std::runtime_error("vkdemo::create_pipeline: pipeline already created");
    }

    auto           folder = std::string(SHADER_FOLDER);
    VkShaderModule vert =
        vulkan::create_shader_module(display_provider_->vk_device_, vulkan::read_file(folder + "/demo.vert.spv"));
    VkShaderModule frag =
        vulkan::create_shader_module(display_provider_->vk_device_, vulkan::read_file(folder + "/demo.frag.spv"));

    VkPipelineShaderStageCreateInfo vert_shader_stage_info{
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, // sType
        nullptr,                                             // pNext
        0,                                                   // flags
        VK_SHADER_STAGE_VERTEX_BIT,                          // stage
        vert,                                                // module
        "main",                                              // pName
        nullptr                                              // pSpecializationInfo
    };

    VkPipelineShaderStageCreateInfo frag_shader_stage_info{
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, // sType
        nullptr,                                             // pNext
        0,                                                   // flags
        VK_SHADER_STAGE_FRAGMENT_BIT,                        // stage
        frag,                                                // module
        "main",                                              // pName
        nullptr                                              // pSpecializationInfo
    };

    VkPipelineShaderStageCreateInfo shader_stages[] = {vert_shader_stage_info, frag_shader_stage_info};

    auto binding_description    = vertex::get_binding_description();
    auto attribute_descriptions = vertex::get_attribute_descriptions();

    VkPipelineVertexInputStateCreateInfo vertex_input_info{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, // sType
        nullptr,                                                   // pNext
        0,                                                         // flags
        1,                                                         // vertexBindingDescriptionCount
        &binding_description,                                      // pVertexBindingDescriptions
        static_cast<uint32_t>(attribute_descriptions.size()),      // vertexAttributeDescriptionCount
        attribute_descriptions.data()                              // pVertexAttributeDescriptions
    };

    VkPipelineInputAssemblyStateCreateInfo input_assembly{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, // sType
        nullptr,                                                     // pNext
        0,                                                           // flags
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,                         // topology
        VK_FALSE                                                     // primitiveRestartEnable
    };

    auto per_eye_extent =
        VkExtent2D{display_provider_->swapchain_extent_.width / 2, display_provider_->swapchain_extent_.height};

    VkViewport viewport{
        0.0f,                                      // x
        0.0f,                                      // y
        static_cast<float>(per_eye_extent.width),  // width
        static_cast<float>(per_eye_extent.height), // height
        0.0f,                                      // minDepth
        1.0f                                       // maxDepth
    };

    VkRect2D scissor{
        {0, 0},        // offset
        per_eye_extent // extent
    };

    VkPipelineViewportStateCreateInfo viewport_state{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, // sType
        nullptr,                                               // pNext
        0,                                                     // flags
        1,                                                     // viewportCount
        &viewport,                                             // pViewports
        1,                                                     // scissorCount
        &scissor                                               // pScissors
    };

    VkPipelineRasterizationStateCreateInfo rasterizer{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, // sType
        nullptr,                                                    // pNext
        0,                                                          // flags
        VK_FALSE,                                                   // depthClampEnable
        VK_FALSE,                                                   // rasterizerDiscardEnable
        VK_POLYGON_MODE_FILL,                                       // polygonMode
        VK_CULL_MODE_NONE,                                          // cullMode
        VK_FRONT_FACE_COUNTER_CLOCKWISE,                            // frontFace
        VK_FALSE,                                                   // depthBiasEnable
        0.f,                                                        // depthBiasConstantFactor
        0.f,                                                        // depthBiasClamp
        0.f,                                                        // depthBiasSlopeFactor
        1.0f                                                        // lineWidth
    };

    VkPipelineMultisampleStateCreateInfo multisampling{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, // sType
        nullptr,                                                  // pNext
        0,                                                        // flags
        VK_SAMPLE_COUNT_1_BIT,                                    // rasterizationSamples
        VK_FALSE,                                                 // sampleShadingEnable
        0.f,                                                      // minSampleShading
        nullptr,                                                  // pSampleMask
        0,                                                        // alphaToCoverageEnable
        0                                                         // alphaToOneEnable
    };

    VkPipelineColorBlendAttachmentState color_blend_attachment{
        VK_FALSE, // blendEnable
        {},       // srcColorBlendFactor
        {},       // dstColorBlendFactor
        {},       // colorBlendOp
        {},       // srcAlphaBlendFactor
        {},       // dstAlphaBlendFactor
        {},       // alphaBlendOp
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
            VK_COLOR_COMPONENT_A_BIT // colorWriteMask
    };

    VkPipelineColorBlendAttachmentState depth_blend_attachment{
        VK_FALSE, // blendEnable
        {},       // srcColorBlendFactor
        {},       // dstColorBlendFactor
        {},       // colorBlendOp
        {},       // srcAlphaBlendFactor
        {},       // dstAlphaBlendFactor
        {},       // alphaBlendOp
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
            VK_COLOR_COMPONENT_A_BIT // colorWriteMask
    };

    VkPipelineColorBlendAttachmentState blend_attachments[2] = {color_blend_attachment, depth_blend_attachment};

    VkPipelineColorBlendStateCreateInfo color_blending{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, // sType
        nullptr,                                                  // pNext
        0,                                                        // flags
        0,                                                        // logicOpEnable
        {},                                                       // logicOp
        2,                                                        // attachmentCount
        blend_attachments,                                        // pAttachments
        {0.f, 0.f, 0.f, 0.f}                                      // blendConstants
    };

    VkPushConstantRange push_constant_range{
        VK_SHADER_STAGE_FRAGMENT_BIT, // stageFlags
        0,                            // offset
        sizeof(model_push_constant)   // size
    };

    VkPipelineLayoutCreateInfo pipeline_layout_info{
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, // sType
        nullptr,                                       // pNext
        0,                                             // flags
        1,                                             // setLayoutCount
        &descriptor_set_layout_,                       // pSetLayouts
        1,                                             // pushConstantRangeCount
        &push_constant_range                           // pPushConstantRanges
    };

    VK_ASSERT_SUCCESS(vkCreatePipelineLayout(display_provider_->vk_device_, &pipeline_layout_info, nullptr, &pipeline_layout_))
    deletion_queue_.emplace([=]() {
        vkDestroyPipelineLayout(display_provider_->vk_device_, pipeline_layout_, nullptr);
    });

    VkPipelineDepthStencilStateCreateInfo depth_stencil{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,                                 // sType
        nullptr,                                                                                    // pNext
        0,                                                                                          // flags
        VK_TRUE,                                                                                    // depthTestEnable
        VK_TRUE,                                                                                    // depthWriteEnable
        rendering_params::reverse_z ? VK_COMPARE_OP_GREATER_OR_EQUAL : VK_COMPARE_OP_LESS_OR_EQUAL, // depthCompareOp
        VK_FALSE,                                                                                   // depthBoundsTestEnable
        VK_FALSE,                                                                                   // stencilTestEnable
        {},                                                                                         // front
        {},                                                                                         // back
        0.0f,                                                                                       // minDepthBounds
        1.0f                                                                                        // maxDepthBounds
    };

    VkGraphicsPipelineCreateInfo pipeline_info{
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, // sType
        nullptr,                                         // pNext
        0,                                               // flags
        2,                                               // stageCount
        shader_stages,                                   // pStages
        &vertex_input_info,                              // pVertexInputState
        &input_assembly,                                 // pInputAssemblyState
        nullptr,                                         // pTessellationState
        &viewport_state,                                 // pViewportState
        &rasterizer,                                     // pRasterizationState
        &multisampling,                                  // pMultisampleState
        &depth_stencil,                                  // pDepthStencilState
        &color_blending,                                 // pColorBlendState
        nullptr,                                         // pDynamicState
        pipeline_layout_,                                // layout
        render_pass,                                     // renderPass
        subpass,                                         // subpass
        {},                                              // basePipelineHandle
        0                                                // basePipelineIndex
    };

    VK_ASSERT_SUCCESS(
        vkCreateGraphicsPipelines(display_provider_->vk_device_, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline_))
    deletion_queue_.emplace([=]() {
        vkDestroyPipeline(display_provider_->vk_device_, pipeline_, nullptr);
    });

    vkDestroyShaderModule(display_provider_->vk_device_, vert, nullptr);
    vkDestroyShaderModule(display_provider_->vk_device_, frag, nullptr);
}

[[maybe_unused]] vkdemo_plugin::vkdemo_plugin(const std::string& name, phonebook* pb)
    : plugin{name, pb}
    , vkd_{std::make_shared<vkdemo>(pb)} {
    pb->register_impl<vulkan::app>(std::static_pointer_cast<vkdemo>(vkd_));
}

void vkdemo_plugin::start() {
    vkd_->initialize();
}

PLUGIN_MAIN(vkdemo_plugin)
