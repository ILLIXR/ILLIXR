#include "stereo_renderer.hpp"

#include "color_frag_spv.h"
#include "color_vert_spv.h"
#include "depth_frag_spv.h"
#include "modal_frag_spv.h"
#include "modal_vert_spv.h"
#include "motion_vec_frag_spv.h"
#include "overlay_frag_spv.h"
#include "overlay_vert_spv.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <vector>
using namespace ILLIXR;
using namespace ILLIXR::data_format;

//
// Helpers
//

#define VK_CHECK(expr)                                                                                                         \
    do {                                                                                                                       \
        VkResult _vk_result = (expr);                                                                                          \
        if (_vk_result != VK_SUCCESS) {                                                                                        \
            spdlog::get("illixr")->error("[stereo_renderer] Vulkan error {} at {}:{}", static_cast<int>(_vk_result), __FILE__, \
                                         __LINE__);                                                                            \
            return false;                                                                                                      \
        }                                                                                                                      \
    } while (0)

stereo_renderer::~stereo_renderer() {
    cleanup();
}

void stereo_renderer::set_crop_region(int original_width, int original_height, int padded_width, int padded_height) {
    crop_scale_x_ = static_cast<float>(original_width) / static_cast<float>(padded_width);
    crop_scale_y_ = static_cast<float>(original_height) / static_cast<float>(padded_height);

    spdlog::get("illixr")->info("[stereo_renderer] Crop {}x{} → {}x{} (scale {:.4f},{:.4f})", padded_width, padded_height,
                                original_width, original_height, crop_scale_x_, crop_scale_y_);
}

//
// Initialization
//
bool stereo_renderer::initialize(VkInstance instance, VkPhysicalDevice physical_device, VkDevice device, VkQueue queue,
                                 uint32_t queue_family, VkFormat swapchain_format) {
    if (initialized_) {
        spdlog::get("illixr")->warn("stereo_renderer already initialized");
        return true;
    }

    instance_         = instance;
    physical_device_  = physical_device;
    device_           = device;
    queue_            = queue;
    queue_family_     = queue_family;
    swapchain_format_ = swapchain_format;
    image_cache_.reserve(32);
    // Resolve the Android hardware buffer properties extension.
    vk_get_ahb_properties_ = reinterpret_cast<PFN_vkGetAndroidHardwareBufferPropertiesANDROID>(
        vkGetDeviceProcAddr(device_, "vkGetAndroidHardwareBufferPropertiesANDROID"));
    if (!vk_get_ahb_properties_) {
        spdlog::get("illixr")->error("[stereo_renderer] VK_ANDROID_external_memory_android_hardware_buffer "
                                     "extension not available");
        return false;
    }

    if (!create_render_pass())
        return false;
    if (!create_command_pool())
        return false;
    if (!allocate_command_buffers())
        return false;
    if (!create_descriptor_pool())
        return false;
    if (!create_boba_overlay_resources())
        return false;

    // Fences for render completion
    VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT; // start signalled (nothing in flight)
    for (int i = 0; i < 2; i++) {
        VK_CHECK(vkCreateFence(device_, &fence_info, nullptr, &render_fences_[i]));
    }

    initialized_ = true;
    spdlog::get("illixr")->info("[stereo_renderer] Vulkan renderer initialized");
    return true;
}

void stereo_renderer::destroy_imported_image(imported_image& img) {
    if (img.image_view != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, img.image_view, nullptr);
        img.image_view = VK_NULL_HANDLE;
    }
    if (img.image != VK_NULL_HANDLE) {
        vkDestroyImage(device_, img.image, nullptr);
        img.image = VK_NULL_HANDLE;
    }
    if (img.memory != VK_NULL_HANDLE) {
        vkFreeMemory(device_, img.memory, nullptr);
        img.memory = VK_NULL_HANDLE;
    }
    if (img.sampler != VK_NULL_HANDLE) {
        vkDestroySampler(device_, img.sampler, nullptr);
        img.sampler = VK_NULL_HANDLE;
    }
    if (img.ycbcr_conv != VK_NULL_HANDLE) {
        vkDestroySamplerYcbcrConversion(device_, img.ycbcr_conv, nullptr);
        img.ycbcr_conv = VK_NULL_HANDLE;
    }
}

//
// Render pass
//

bool stereo_renderer::create_render_pass() {
    VkAttachmentDescription color_attachment{};
    color_attachment.format         = swapchain_format_;
    color_attachment.samples        = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    // OpenXR expects COLOR_ATTACHMENT_OPTIMAL when it acquires the image back.
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference color_ref{};
    color_ref.attachment = 0;
    color_ref.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &color_ref;

    VkSubpassDependency dependency{};
    dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass    = 0;
    dependency.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rp_info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rp_info.attachmentCount = 1;
    rp_info.pAttachments    = &color_attachment;
    rp_info.subpassCount    = 1;
    rp_info.pSubpasses      = &subpass;
    rp_info.dependencyCount = 1;
    rp_info.pDependencies   = &dependency;

    VK_CHECK(vkCreateRenderPass(device_, &rp_info, nullptr, &render_pass_));
    return true;
}

//
// Pipeline (deferred — created on first frame, once we have a prototype image
// from which we can extract the external format and build the sampler)
//

bool stereo_renderer::create_pipeline(const imported_image& prototype) {
    if (pipeline_created_)
        return true;

    // Descriptor set layout with immutable YCbCr sampler
    // YCbCr combined-image-samplers MUST use immutable samplers in the layout.
    VkDescriptorSetLayoutBinding binding{};
    binding.binding            = 0;
    binding.descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount    = 1;
    binding.stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT;
    binding.pImmutableSamplers = &prototype.sampler;

    VkDescriptorSetLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout_info.bindingCount = 1;
    layout_info.pBindings    = &binding;
    VK_CHECK(vkCreateDescriptorSetLayout(device_, &layout_info, nullptr, &desc_set_layout_));

    // Allocate descriptor sets (one per eye)
    std::vector<VkDescriptorSetLayout> layouts(2, desc_set_layout_);
    VkDescriptorSetAllocateInfo        alloc_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc_info.descriptorPool     = descriptor_pool_;
    alloc_info.descriptorSetCount = 2;
    alloc_info.pSetLayouts        = layouts.data();
    VK_CHECK(vkAllocateDescriptorSets(device_, &alloc_info, descriptor_sets_.data()));

    // Push constants (crop scale + u_offset)
    VkPushConstantRange push_range{};
    push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push_range.offset     = 0;
#ifdef COMBINED_ENCODING
    push_range.size = sizeof(float) * 3; // crop_scale_x, crop_scale_y, u_offset
#else
    push_range.size = sizeof(float) * 2; // crop_scale_x, crop_scale_y
#endif

    VkPipelineLayoutCreateInfo pl_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl_info.setLayoutCount         = 1;
    pl_info.pSetLayouts            = &desc_set_layout_;
    pl_info.pushConstantRangeCount = 1;
    pl_info.pPushConstantRanges    = &push_range;
    VK_CHECK(vkCreatePipelineLayout(device_, &pl_info, nullptr, &pipeline_layout_));

    // Shaders
    VkShaderModule vert_module = create_shader_module(device_, color_vert_spv, sizeof(color_vert_spv) / sizeof(uint32_t));
    VkShaderModule frag_module = create_shader_module(device_, color_frag_spv, sizeof(color_frag_spv) / sizeof(uint32_t));
    if (vert_module == VK_NULL_HANDLE || frag_module == VK_NULL_HANDLE) {
        spdlog::get("illixr")->error("[stereo_renderer] Shader compilation failed");
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert_module;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag_module;
    stages[1].pName  = "main";

    // Fixed-function state
    VkPipelineVertexInputStateCreateInfo vertex_input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    // No vertex buffers — we generate vertices from gl_VertexIndex.

    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp_state{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp_state.viewportCount = 1;
    vp_state.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode    = VK_CULL_MODE_NONE;
    raster.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blend_attachment{};
    blend_attachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1;
    blend.pAttachments    = &blend_attachment;

    // Viewport and scissor are dynamic so we can change them per-frame.
    constexpr VkDynamicState         dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates    = dyn_states;

    VkGraphicsPipelineCreateInfo gp_info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gp_info.stageCount          = 2;
    gp_info.pStages             = stages;
    gp_info.pVertexInputState   = &vertex_input;
    gp_info.pInputAssemblyState = &ia;
    gp_info.pViewportState      = &vp_state;
    gp_info.pRasterizationState = &raster;
    gp_info.pMultisampleState   = &ms;
    gp_info.pColorBlendState    = &blend;
    gp_info.pDynamicState       = &dyn;
    gp_info.layout              = pipeline_layout_;
    gp_info.renderPass          = render_pass_;
    gp_info.subpass             = 0;

    VkResult result = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp_info, nullptr, &pipeline_);
    vkDestroyShaderModule(device_, vert_module, nullptr);
    vkDestroyShaderModule(device_, frag_module, nullptr);

    if (result != VK_SUCCESS) {
        spdlog::get("illixr")->error("[stereo_renderer] vkCreateGraphicsPipelines failed: {}", static_cast<int>(result));
        // Release all resources allocated in this attempt so that:
        //   (a) the descriptor pool does not fill up on repeated failures
        //       (pool exhaustion returns VK_ERROR_OUT_OF_POOL_MEMORY = -1000069000
        //        from vkAllocateDescriptorSets on the next call), and
        //   (b) the next attempt starts from a clean slate.
        vkFreeDescriptorSets(device_, descriptor_pool_, 2, descriptor_sets_.data());
        descriptor_sets_.fill(VK_NULL_HANDLE);
        vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
        pipeline_layout_ = VK_NULL_HANDLE;
        vkDestroyDescriptorSetLayout(device_, desc_set_layout_, nullptr);
        desc_set_layout_ = VK_NULL_HANDLE;
        return false;
    }

    pipeline_created_ = true;
    spdlog::get("illixr")->info("[stereo_renderer] Pipeline created");
    return true;
}

std::uint32_t stereo_renderer::find_memory_type(std::uint32_t type_filter, VkMemoryPropertyFlags properties) const {
    VkPhysicalDeviceMemoryProperties memory_properties{};
    vkGetPhysicalDeviceMemoryProperties(physical_device_, &memory_properties);
    for (std::uint32_t index = 0; index < memory_properties.memoryTypeCount; ++index) {
        if ((type_filter & (1U << index)) != 0 &&
            (memory_properties.memoryTypes[index].propertyFlags & properties) == properties) {
            return index;
        }
    }
    return UINT32_MAX;
}

bool stereo_renderer::create_host_visible_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer* buffer,
                                                 VkDeviceMemory* memory, void** mapped) {
    VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer_info.size        = size;
    buffer_info.usage       = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device_, &buffer_info, nullptr, buffer) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device_, *buffer, &requirements);
    const std::uint32_t memory_type = find_memory_type(
        requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (memory_type == UINT32_MAX) {
        vkDestroyBuffer(device_, *buffer, nullptr);
        *buffer = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize  = requirements.size;
    allocation.memoryTypeIndex = memory_type;
    if (vkAllocateMemory(device_, &allocation, nullptr, memory) != VK_SUCCESS) {
        vkDestroyBuffer(device_, *buffer, nullptr);
        *buffer = VK_NULL_HANDLE;
        return false;
    }
    if (vkBindBufferMemory(device_, *buffer, *memory, 0) != VK_SUCCESS ||
        vkMapMemory(device_, *memory, 0, size, 0, mapped) != VK_SUCCESS) {
        vkDestroyBuffer(device_, *buffer, nullptr);
        vkFreeMemory(device_, *memory, nullptr);
        *memory = VK_NULL_HANDLE;
        *buffer = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

bool stereo_renderer::create_overlay_pipeline() {
    VkPushConstantRange push_range{};
    push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push_range.size       = 2 * sizeof(float);

    VkPipelineLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges    = &push_range;
    VK_CHECK(vkCreatePipelineLayout(device_, &layout_info, nullptr, &overlay_pipeline_layout_));

    VkShaderModule vert = create_shader_module(device_, overlay_vert_spv, sizeof(overlay_vert_spv) / sizeof(uint32_t));
    VkShaderModule frag = create_shader_module(device_, overlay_frag_spv, sizeof(overlay_frag_spv) / sizeof(uint32_t));
    if (vert == VK_NULL_HANDLE || frag == VK_NULL_HANDLE) {
        return false;
    }
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vert, "main", nullptr};
    stages[1] = {
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, frag, "main", nullptr};

    VkVertexInputBindingDescription binding{};
    binding.binding   = 0;
    binding.stride    = sizeof(overlay_vertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    std::array<VkVertexInputAttributeDescription, 2> attributes{};
    attributes[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(overlay_vertex, x)};
    attributes[1] = {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(overlay_vertex, red)};

    VkPipelineVertexInputStateCreateInfo vertex_input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertex_input.vertexBindingDescriptionCount   = 1;
    vertex_input.pVertexBindingDescriptions      = &binding;
    vertex_input.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
    vertex_input.pVertexAttributeDescriptions    = attributes.data();

    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount  = 1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode    = VK_CULL_MODE_NONE;
    raster.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth   = 1.0F;
    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blend_attachment{};
    blend_attachment.blendEnable         = VK_TRUE;
    blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend_attachment.colorBlendOp        = VK_BLEND_OP_ADD;
    blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend_attachment.alphaBlendOp        = VK_BLEND_OP_ADD;
    blend_attachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1;
    blend.pAttachments    = &blend_attachment;

    constexpr VkDynamicState         dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates    = dynamic_states;

    VkGraphicsPipelineCreateInfo pipeline_info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipeline_info.stageCount          = 2;
    pipeline_info.pStages             = stages;
    pipeline_info.pVertexInputState   = &vertex_input;
    pipeline_info.pInputAssemblyState = &assembly;
    pipeline_info.pViewportState      = &viewport;
    pipeline_info.pRasterizationState = &raster;
    pipeline_info.pMultisampleState   = &multisample;
    pipeline_info.pColorBlendState    = &blend;
    pipeline_info.pDynamicState       = &dynamic;
    pipeline_info.layout              = overlay_pipeline_layout_;
    pipeline_info.renderPass          = render_pass_;
    pipeline_info.subpass             = 0;
    const VkResult result = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &overlay_pipeline_);
    vkDestroyShaderModule(device_, vert, nullptr);
    vkDestroyShaderModule(device_, frag, nullptr);
    return result == VK_SUCCESS;
}

bool stereo_renderer::create_modal_pipeline() {
    VkSamplerCreateInfo sampler_info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler_info.magFilter               = VK_FILTER_LINEAR;
    sampler_info.minFilter               = VK_FILTER_LINEAR;
    sampler_info.addressModeU            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.unnormalizedCoordinates = VK_FALSE;
    VK_CHECK(vkCreateSampler(device_, &sampler_info, nullptr, &modal_sampler_));

    VkDescriptorSetLayoutBinding descriptor_binding{};
    descriptor_binding.binding         = 0;
    descriptor_binding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptor_binding.descriptorCount = 1;
    descriptor_binding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo descriptor_layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    descriptor_layout.bindingCount = 1;
    descriptor_layout.pBindings    = &descriptor_binding;
    VK_CHECK(vkCreateDescriptorSetLayout(device_, &descriptor_layout, nullptr, &modal_desc_set_layout_));

    VkDescriptorPoolSize pool_size{};
    pool_size.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_size.descriptorCount = 1;
    VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool_info.maxSets       = 1;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes    = &pool_size;
    VK_CHECK(vkCreateDescriptorPool(device_, &pool_info, nullptr, &modal_descriptor_pool_));
    VkDescriptorSetAllocateInfo descriptor_allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    descriptor_allocation.descriptorPool     = modal_descriptor_pool_;
    descriptor_allocation.descriptorSetCount = 1;
    descriptor_allocation.pSetLayouts        = &modal_desc_set_layout_;
    VK_CHECK(vkAllocateDescriptorSets(device_, &descriptor_allocation, &modal_descriptor_set_));

    VkPushConstantRange push_range{};
    push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push_range.size       = 2 * sizeof(float);
    VkPipelineLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout_info.setLayoutCount         = 1;
    layout_info.pSetLayouts            = &modal_desc_set_layout_;
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges    = &push_range;
    VK_CHECK(vkCreatePipelineLayout(device_, &layout_info, nullptr, &modal_pipeline_layout_));

    VkShaderModule vert = create_shader_module(device_, modal_vert_spv, sizeof(modal_vert_spv) / sizeof(uint32_t));
    VkShaderModule frag = create_shader_module(device_, modal_frag_spv, sizeof(modal_frag_spv) / sizeof(uint32_t));
    if (vert == VK_NULL_HANDLE || frag == VK_NULL_HANDLE) {
        return false;
    }
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vert, "main", nullptr};
    stages[1] = {
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, frag, "main", nullptr};

    VkVertexInputBindingDescription binding{};
    binding.binding   = 0;
    binding.stride    = sizeof(modal_vertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    std::array<VkVertexInputAttributeDescription, 2> attributes{};
    attributes[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(modal_vertex, x)};
    attributes[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(modal_vertex, u)};
    VkPipelineVertexInputStateCreateInfo vertex_input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertex_input.vertexBindingDescriptionCount   = 1;
    vertex_input.pVertexBindingDescriptions      = &binding;
    vertex_input.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
    vertex_input.pVertexAttributeDescriptions    = attributes.data();

    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount  = 1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode    = VK_CULL_MODE_NONE;
    raster.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth   = 1.0F;
    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blend_attachment{};
    blend_attachment.blendEnable         = VK_TRUE;
    blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend_attachment.colorBlendOp        = VK_BLEND_OP_ADD;
    blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend_attachment.alphaBlendOp        = VK_BLEND_OP_ADD;
    blend_attachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount                             = 1;
    blend.pAttachments                                = &blend_attachment;
    constexpr VkDynamicState         dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates    = dynamic_states;

    VkGraphicsPipelineCreateInfo pipeline_info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipeline_info.stageCount          = 2;
    pipeline_info.pStages             = stages;
    pipeline_info.pVertexInputState   = &vertex_input;
    pipeline_info.pInputAssemblyState = &assembly;
    pipeline_info.pViewportState      = &viewport;
    pipeline_info.pRasterizationState = &raster;
    pipeline_info.pMultisampleState   = &multisample;
    pipeline_info.pColorBlendState    = &blend;
    pipeline_info.pDynamicState       = &dynamic;
    pipeline_info.layout              = modal_pipeline_layout_;
    pipeline_info.renderPass          = render_pass_;
    pipeline_info.subpass             = 0;
    const VkResult result = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &modal_pipeline_);
    vkDestroyShaderModule(device_, vert, nullptr);
    vkDestroyShaderModule(device_, frag, nullptr);
    return result == VK_SUCCESS;
}

bool stereo_renderer::create_boba_overlay_resources() {
    if (!create_overlay_pipeline() || !create_modal_pipeline()) {
        spdlog::get("illixr")->error("[stereo_renderer] Could not create Boba overlay pipelines");
        return false;
    }

    constexpr VkDeviceSize overlay_buffer_bytes =
        sizeof(overlay_vertex) * data_format::boba_frame_overlay::max_commands_per_eye * 6ULL;
    constexpr VkDeviceSize modal_buffer_bytes = sizeof(modal_vertex) * 6ULL;
    for (int eye = 0; eye < 2; ++eye) {
        if (!create_host_visible_buffer(overlay_buffer_bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &overlay_vertex_buffers_[eye],
                                        &overlay_vertex_memories_[eye], &overlay_vertex_mapped_[eye]) ||
            !create_host_visible_buffer(modal_buffer_bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &modal_vertex_buffers_[eye],
                                        &modal_vertex_memories_[eye], &modal_vertex_mapped_[eye])) {
            spdlog::get("illixr")->error("[stereo_renderer] Could not allocate Boba overlay vertex buffers");
            return false;
        }
        overlay_vertices_[eye].reserve(data_format::boba_frame_overlay::max_commands_per_eye * 6ULL);
    }
    spdlog::get("illixr")->info("[stereo_renderer] Boba overlay pipelines initialized");
    return true;
}

void stereo_renderer::destroy_modal_texture() {
    if (modal_image_view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, modal_image_view_, nullptr);
        modal_image_view_ = VK_NULL_HANDLE;
    }
    if (modal_image_ != VK_NULL_HANDLE) {
        vkDestroyImage(device_, modal_image_, nullptr);
        modal_image_ = VK_NULL_HANDLE;
    }
    if (modal_image_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device_, modal_image_memory_, nullptr);
        modal_image_memory_ = VK_NULL_HANDLE;
    }
    modal_texture_id_ = 0;
}

bool stereo_renderer::upload_modal_texture(std::uint64_t texture_id, std::uint32_t width, std::uint32_t height,
                                           const std::vector<std::uint8_t>& rgba) {
    const std::uint64_t expected_size = static_cast<std::uint64_t>(width) * height * 4ULL;
    if (texture_id == 0 || width == 0 || height == 0 || expected_size != rgba.size()) {
        return false;
    }

    VkBuffer       staging_buffer = VK_NULL_HANDLE;
    VkDeviceMemory staging_memory = VK_NULL_HANDLE;
    void*          staging_mapped = nullptr;
    if (!create_host_visible_buffer(expected_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &staging_buffer, &staging_memory,
                                    &staging_mapped)) {
        spdlog::get("illixr")->error("[stereo_renderer] Could not allocate Boba modal staging buffer");
        return false;
    }
    std::memcpy(staging_mapped, rgba.data(), rgba.size());
    vkUnmapMemory(device_, staging_memory);
    staging_mapped = nullptr;

    VkImage         new_image      = VK_NULL_HANDLE;
    VkDeviceMemory  new_memory     = VK_NULL_HANDLE;
    VkImageView     new_view       = VK_NULL_HANDLE;
    VkCommandBuffer upload_command = VK_NULL_HANDLE;

    const auto release_new_resources = [&] {
        if (upload_command != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(device_, command_pool_, 1, &upload_command);
            upload_command = VK_NULL_HANDLE;
        }
        if (new_view != VK_NULL_HANDLE) {
            vkDestroyImageView(device_, new_view, nullptr);
            new_view = VK_NULL_HANDLE;
        }
        if (new_image != VK_NULL_HANDLE) {
            vkDestroyImage(device_, new_image, nullptr);
            new_image = VK_NULL_HANDLE;
        }
        if (new_memory != VK_NULL_HANDLE) {
            vkFreeMemory(device_, new_memory, nullptr);
            new_memory = VK_NULL_HANDLE;
        }
        if (staging_buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_, staging_buffer, nullptr);
            staging_buffer = VK_NULL_HANDLE;
        }
        if (staging_memory != VK_NULL_HANDLE) {
            vkFreeMemory(device_, staging_memory, nullptr);
            staging_memory = VK_NULL_HANDLE;
        }
    };

    VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image_info.imageType     = VK_IMAGE_TYPE_2D;
    image_info.format        = VK_FORMAT_R8G8B8A8_UNORM;
    image_info.extent        = {width, height, 1};
    image_info.mipLevels     = 1;
    image_info.arrayLayers   = 1;
    image_info.samples       = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling        = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device_, &image_info, nullptr, &new_image) != VK_SUCCESS) {
        release_new_resources();
        return false;
    }

    VkMemoryRequirements image_requirements{};
    vkGetImageMemoryRequirements(device_, new_image, &image_requirements);
    const std::uint32_t image_memory_type =
        find_memory_type(image_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (image_memory_type == UINT32_MAX) {
        release_new_resources();
        return false;
    }

    VkMemoryAllocateInfo image_allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    image_allocation.allocationSize  = image_requirements.size;
    image_allocation.memoryTypeIndex = image_memory_type;
    if (vkAllocateMemory(device_, &image_allocation, nullptr, &new_memory) != VK_SUCCESS ||
        vkBindImageMemory(device_, new_image, new_memory, 0) != VK_SUCCESS) {
        release_new_resources();
        return false;
    }

    VkCommandBufferAllocateInfo command_allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    command_allocation.commandPool        = command_pool_;
    command_allocation.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_allocation.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(device_, &command_allocation, &upload_command) != VK_SUCCESS) {
        release_new_resources();
        return false;
    }

    VkCommandBufferBeginInfo command_begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    command_begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(upload_command, &command_begin) != VK_SUCCESS) {
        release_new_resources();
        return false;
    }

    VkImageMemoryBarrier to_transfer{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    to_transfer.oldLayout                   = VK_IMAGE_LAYOUT_UNDEFINED;
    to_transfer.newLayout                   = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_transfer.srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
    to_transfer.dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
    to_transfer.image                       = new_image;
    to_transfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    to_transfer.subresourceRange.levelCount = 1;
    to_transfer.subresourceRange.layerCount = 1;
    to_transfer.dstAccessMask               = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(upload_command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &to_transfer);

    VkBufferImageCopy copy_region{};
    copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy_region.imageSubresource.layerCount = 1;
    copy_region.imageExtent                 = {width, height, 1};
    vkCmdCopyBufferToImage(upload_command, staging_buffer, new_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_region);

    VkImageMemoryBarrier to_shader_read{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    to_shader_read.oldLayout                   = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_shader_read.newLayout                   = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    to_shader_read.srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
    to_shader_read.dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
    to_shader_read.image                       = new_image;
    to_shader_read.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    to_shader_read.subresourceRange.levelCount = 1;
    to_shader_read.subresourceRange.layerCount = 1;
    to_shader_read.srcAccessMask               = VK_ACCESS_TRANSFER_WRITE_BIT;
    to_shader_read.dstAccessMask               = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(upload_command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                         0, nullptr, 1, &to_shader_read);

    if (vkEndCommandBuffer(upload_command) != VK_SUCCESS) {
        release_new_resources();
        return false;
    }
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &upload_command;
    if (vkQueueSubmit(queue_, 1, &submit, VK_NULL_HANDLE) != VK_SUCCESS || vkQueueWaitIdle(queue_) != VK_SUCCESS) {
        release_new_resources();
        return false;
    }

    VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view_info.image                           = new_image;
    view_info.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format                          = VK_FORMAT_R8G8B8A8_UNORM;
    view_info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel   = 0;
    view_info.subresourceRange.levelCount     = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount     = 1;
    if (vkCreateImageView(device_, &view_info, nullptr, &new_view) != VK_SUCCESS) {
        release_new_resources();
        return false;
    }

    // Queue-idle above guarantees the previous modal is no longer referenced.
    destroy_modal_texture();
    modal_image_        = new_image;
    modal_image_memory_ = new_memory;
    modal_image_view_   = new_view;
    modal_texture_id_   = texture_id;
    new_image           = VK_NULL_HANDLE;
    new_memory          = VK_NULL_HANDLE;
    new_view            = VK_NULL_HANDLE;

    VkDescriptorImageInfo descriptor_image{};
    descriptor_image.sampler     = modal_sampler_;
    descriptor_image.imageView   = modal_image_view_;
    descriptor_image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet descriptor_write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    descriptor_write.dstSet          = modal_descriptor_set_;
    descriptor_write.dstBinding      = 0;
    descriptor_write.descriptorCount = 1;
    descriptor_write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptor_write.pImageInfo      = &descriptor_image;
    vkUpdateDescriptorSets(device_, 1, &descriptor_write, 0, nullptr);

    release_new_resources();
    spdlog::get("illixr")->info("[stereo_renderer] Uploaded Boba modal texture id={} size={}x{}", texture_id, width, height);
    return true;
}

void stereo_renderer::update_boba_overlay_state(const data_format::dual_frames& frame) {
    overlay_source_width_  = frame.boba_overlay.source_width;
    overlay_source_height_ = frame.boba_overlay.source_height;
    render_boba_overlays_  = frame.presentation_mode == data_format::stereo_presentation_mode::stereo_fullscreen &&
        overlay_source_width_ > 0 && overlay_source_height_ > 0;
    active_modal_ = frame.boba_modal;

    const auto append_vertex = [](std::vector<overlay_vertex>& vertices, float x, float y, float red, float green, float blue,
                                  float alpha) {
        vertices.push_back({x, y, std::clamp(red / 255.0F, 0.0F, 1.0F), std::clamp(green / 255.0F, 0.0F, 1.0F),
                            std::clamp(blue / 255.0F, 0.0F, 1.0F), std::clamp(alpha, 0.0F, 1.0F)});
    };
    const auto append_triangle = [&](std::vector<overlay_vertex>& vertices, float x0, float y0, float x1, float y1, float x2,
                                     float y2, float red, float green, float blue, float alpha) {
        append_vertex(vertices, x0, y0, red, green, blue, alpha);
        append_vertex(vertices, x1, y1, red, green, blue, alpha);
        append_vertex(vertices, x2, y2, red, green, blue, alpha);
    };

    const std::array<const std::vector<float>*, 2> command_lists = {&frame.boba_overlay.left_commands,
                                                                    &frame.boba_overlay.right_commands};
    for (int eye = 0; eye < 2; ++eye) {
        auto& vertices = overlay_vertices_[eye];
        vertices.clear();
        modal_vertex_counts_[eye] = 0;
        if (!render_boba_overlays_) {
            continue;
        }

        const auto&       commands = *command_lists[eye];
        const std::size_t command_count =
            std::min<std::size_t>(commands.size() / data_format::boba_frame_overlay::command_stride_floats,
                                  data_format::boba_frame_overlay::max_commands_per_eye);
        for (std::size_t command_index = 0; command_index < command_count; ++command_index) {
            const float* command = commands.data() + command_index * data_format::boba_frame_overlay::command_stride_floats;
            if (!std::all_of(command, command + 10, [](float value) {
                    return std::isfinite(value);
                })) {
                continue;
            }

            const int   command_type = static_cast<int>(std::round(command[0]));
            const float alpha        = command[6];
            const float red          = command[7];
            const float green        = command[8];
            const float blue         = command[9];
            if (command_type == 0) {
                const float start_x = command[1];
                const float start_y = command[2];
                const float end_x   = command[3];
                const float end_y   = command[4];
                const float radius  = std::max(0.5F, command[5]);
                const float delta_x = end_x - start_x;
                const float delta_y = end_y - start_y;
                const float length  = std::sqrt(delta_x * delta_x + delta_y * delta_y);
                if (length <= 1.0e-4F) {
                    continue;
                }
                const float normal_x = -delta_y / length * radius;
                const float normal_y = delta_x / length * radius;
                append_triangle(vertices, start_x + normal_x, start_y + normal_y, end_x + normal_x, end_y + normal_y,
                                end_x - normal_x, end_y - normal_y, red, green, blue, alpha);
                append_triangle(vertices, start_x + normal_x, start_y + normal_y, end_x - normal_x, end_y - normal_y,
                                start_x - normal_x, start_y - normal_y, red, green, blue, alpha);
            } else if (command_type == 1) {
                const float center_x = command[1];
                const float center_y = command[2];
                const float radius   = std::max(1.0F, command[5]);
                const float x0       = center_x - radius;
                const float y0       = center_y - radius;
                const float x1       = center_x + radius;
                const float y1       = center_y + radius;
                append_triangle(vertices, x0, y0, x1, y0, x1, y1, red, green, blue, alpha);
                append_triangle(vertices, x0, y0, x1, y1, x0, y1, red, green, blue, alpha);
            }
        }

        const bool  eye_valid = eye == 0 ? active_modal_.left_valid : active_modal_.right_valid;
        const auto& quad      = eye == 0 ? active_modal_.left_quad_pixels : active_modal_.right_quad_pixels;
        if (active_modal_.visible && eye_valid && std::all_of(quad.begin(), quad.end(), [](float value) {
                return std::isfinite(value);
            })) {
            modal_vertices_[eye]      = {{{quad[0], quad[1], 0.0F, 0.0F},
                                          {quad[2], quad[3], 1.0F, 0.0F},
                                          {quad[4], quad[5], 1.0F, 1.0F},
                                          {quad[0], quad[1], 0.0F, 0.0F},
                                          {quad[4], quad[5], 1.0F, 1.0F},
                                          {quad[6], quad[7], 0.0F, 1.0F}}};
            modal_vertex_counts_[eye] = 6;
        }
    }

    if (render_boba_overlays_ && active_modal_.visible && active_modal_.texture_id != 0 &&
        active_modal_.texture_id != modal_texture_id_ && frame.boba_modal_rgba != nullptr) {
        const std::uint64_t expected_size = static_cast<std::uint64_t>(active_modal_.width) * active_modal_.height * 4ULL;
        if (expected_size == frame.boba_modal_rgba->size() &&
            !upload_modal_texture(active_modal_.texture_id, active_modal_.width, active_modal_.height,
                                  *frame.boba_modal_rgba)) {
            spdlog::get("illixr")->warn("[stereo_renderer] Could not upload Boba modal texture id={}",
                                        active_modal_.texture_id);
        }
    }
}

void stereo_renderer::record_boba_overlays(VkCommandBuffer command_buffer, int eye) {
    if (!render_boba_overlays_ || eye < 0 || eye > 1) {
        return;
    }

    const float source_size[2] = {static_cast<float>(overlay_source_width_), static_cast<float>(overlay_source_height_)};
    const auto& overlay        = overlay_vertices_[eye];
    if (!overlay.empty()) {
        std::memcpy(overlay_vertex_mapped_[eye], overlay.data(), overlay.size() * sizeof(overlay_vertex));
        const VkDeviceSize offset = 0;
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, overlay_pipeline_);
        vkCmdPushConstants(command_buffer, overlay_pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(source_size),
                           source_size);
        vkCmdBindVertexBuffers(command_buffer, 0, 1, &overlay_vertex_buffers_[eye], &offset);
        vkCmdDraw(command_buffer, static_cast<std::uint32_t>(overlay.size()), 1, 0, 0);
    }

    if (active_modal_.visible && modal_vertex_counts_[eye] == 6 && modal_texture_id_ == active_modal_.texture_id &&
        modal_image_view_ != VK_NULL_HANDLE) {
        std::memcpy(modal_vertex_mapped_[eye], modal_vertices_[eye].data(), sizeof(modal_vertices_[eye]));
        const VkDeviceSize offset = 0;
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, modal_pipeline_);
        vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, modal_pipeline_layout_, 0, 1,
                                &modal_descriptor_set_, 0, nullptr);
        vkCmdPushConstants(command_buffer, modal_pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(source_size),
                           source_size);
        vkCmdBindVertexBuffers(command_buffer, 0, 1, &modal_vertex_buffers_[eye], &offset);
        vkCmdDraw(command_buffer, 6, 1, 0, 0);
    }
}

//
// Shader module
//

VkShaderModule stereo_renderer::create_shader_module(VkDevice device, const uint32_t* spv, size_t word_count) {
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize        = word_count * sizeof(uint32_t);
    ci.pCode           = spv;
    VkShaderModule mod = VK_NULL_HANDLE;
    vkCreateShaderModule(device, &ci, nullptr, &mod);
    return mod;
}

//
// Command pool and buffers
//

bool stereo_renderer::create_command_pool() {
    VkCommandPoolCreateInfo info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    info.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    info.queueFamilyIndex = queue_family_;
    VK_CHECK(vkCreateCommandPool(device_, &info, nullptr, &command_pool_));
    return true;
}

bool stereo_renderer::allocate_command_buffers() {
    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.commandPool        = command_pool_;
    alloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 2;
    VK_CHECK(vkAllocateCommandBuffers(device_, &alloc, command_buffers_.data()));
    return true;
}

bool stereo_renderer::create_descriptor_pool() {
    // Two eyes × one descriptor per eye.  Extra capacity for re-allocation
    // when the pipeline is rebuilt after the first frame.
    VkDescriptorPoolSize pool_size{};
    pool_size.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_size.descriptorCount = 8;

    VkDescriptorPoolCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    info.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    info.maxSets       = 8;
    info.poolSizeCount = 1;
    info.pPoolSizes    = &pool_size;
    VK_CHECK(vkCreateDescriptorPool(device_, &info, nullptr, &descriptor_pool_));
    return true;
}

//
// AHardwareBuffer → VkImage import
//

stereo_renderer::imported_image* stereo_renderer::import_hardware_buffer(AHardwareBuffer* hw_buffer) {
    auto it = image_cache_.find(hw_buffer);
    if (it != image_cache_.end()) {
        return it->second.get();
    }

    imported_image img{};
    img.hw_buffer = hw_buffer;

    // Query AHardwareBuffer properties
    VkAndroidHardwareBufferFormatPropertiesANDROID fmt_props{
        VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID};
    VkAndroidHardwareBufferPropertiesANDROID ahb_props{VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID};
    ahb_props.pNext = &fmt_props;

    if (vk_get_ahb_properties_(device_, hw_buffer, &ahb_props) != VK_SUCCESS) {
        spdlog::get("illixr")->error("[stereo_renderer] vkGetAndroidHardwareBufferPropertiesANDROID failed");
        return nullptr;
    }

    img.external_fmt = fmt_props.externalFormat;
    spdlog::get("illixr")->debug("[stereo_renderer] AHardwareBuffer imported: "
                                 "externalFormat=0x{:X} memTypeBits=0x{:X}",
                                 img.external_fmt, ahb_props.memoryTypeBits);

    // YCbCr conversion
    // For AIMAGE_FORMAT_PRIVATE buffers the externalFormat is non-zero.
    // We must attach a VkSamplerYcbcrConversion so the driver performs
    // the YUV→RGB transform in the sampler.
    VkExternalFormatANDROID ext_fmt_info{VK_STRUCTURE_TYPE_EXTERNAL_FORMAT_ANDROID};
    ext_fmt_info.externalFormat = img.external_fmt;

    VkSamplerYcbcrConversionCreateInfo ycbcr_info{VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO};
    ycbcr_info.pNext                       = &ext_fmt_info;
    ycbcr_info.format                      = VK_FORMAT_UNDEFINED; // required for external format
    ycbcr_info.ycbcrModel                  = fmt_props.suggestedYcbcrModel;
    ycbcr_info.ycbcrRange                  = fmt_props.suggestedYcbcrRange;
    ycbcr_info.components                  = fmt_props.samplerYcbcrConversionComponents;
    ycbcr_info.xChromaOffset               = fmt_props.suggestedXChromaOffset;
    ycbcr_info.yChromaOffset               = fmt_props.suggestedYChromaOffset;
    ycbcr_info.chromaFilter                = VK_FILTER_LINEAR;
    ycbcr_info.forceExplicitReconstruction = VK_FALSE;

    if (vkCreateSamplerYcbcrConversion(device_, &ycbcr_info, nullptr, &img.ycbcr_conv) != VK_SUCCESS) {
        spdlog::get("illixr")->error("[stereo_renderer] vkCreateSamplerYcbcrConversion failed");
        return nullptr;
    }

    // Sampler
    VkSamplerYcbcrConversionInfo conv_info{VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO};
    conv_info.conversion = img.ycbcr_conv;

    VkSamplerCreateInfo sampler_info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler_info.pNext                   = &conv_info;
    sampler_info.magFilter               = VK_FILTER_LINEAR;
    sampler_info.minFilter               = VK_FILTER_LINEAR;
    sampler_info.addressModeU            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.unnormalizedCoordinates = VK_FALSE;

    if (vkCreateSampler(device_, &sampler_info, nullptr, &img.sampler) != VK_SUCCESS) {
        spdlog::get("illixr")->error("[stereo_renderer] vkCreateSampler failed");
        destroy_imported_image(img);
        return nullptr;
    }

    // VkImage
    AHardwareBuffer_Desc ahb_desc{};
    AHardwareBuffer_describe(hw_buffer, &ahb_desc);

    VkExternalMemoryImageCreateInfo ext_mem_img{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
    ext_mem_img.pNext       = &ext_fmt_info;
    ext_mem_img.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;

    VkImageCreateInfo img_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    img_info.pNext         = &ext_mem_img;
    img_info.imageType     = VK_IMAGE_TYPE_2D;
    img_info.format        = VK_FORMAT_UNDEFINED; // external format
    img_info.extent        = {ahb_desc.width, ahb_desc.height, 1};
    img_info.mipLevels     = 1;
    img_info.arrayLayers   = 1;
    img_info.samples       = VK_SAMPLE_COUNT_1_BIT;
    img_info.tiling        = VK_IMAGE_TILING_OPTIMAL;
    img_info.usage         = VK_IMAGE_USAGE_SAMPLED_BIT;
    img_info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    img_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(device_, &img_info, nullptr, &img.image) != VK_SUCCESS) {
        spdlog::get("illixr")->error("[stereo_renderer] vkCreateImage (AHB) failed");
        destroy_imported_image(img);
        return nullptr;
    }

    // VkDeviceMemory (imported from AHardwareBuffer)
    VkImportAndroidHardwareBufferInfoANDROID import_info{VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID};
    import_info.buffer = hw_buffer;

    VkMemoryDedicatedAllocateInfo ded_alloc{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
    ded_alloc.pNext = &import_info;
    ded_alloc.image = img.image;

    // Find a memory type that satisfies the AHardwareBuffer requirements.
    uint32_t                         mem_type_idx = 0;
    VkPhysicalDeviceMemoryProperties mem_props{};
    vkGetPhysicalDeviceMemoryProperties(physical_device_, &mem_props);
    bool found = false;
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        if ((ahb_props.memoryTypeBits & (1u << i)) &&
            (mem_props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            mem_type_idx = i;
            found        = true;
            break;
        }
    }
    if (!found) {
        // Fallback: use any matching type
        for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
            if (ahb_props.memoryTypeBits & (1u << i)) {
                mem_type_idx = i;
                break;
            }
        }
    }

    VkMemoryAllocateInfo mem_alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mem_alloc.pNext           = &ded_alloc;
    mem_alloc.allocationSize  = ahb_props.allocationSize;
    mem_alloc.memoryTypeIndex = mem_type_idx;

    if (vkAllocateMemory(device_, &mem_alloc, nullptr, &img.memory) != VK_SUCCESS) {
        spdlog::get("illixr")->error("[stereo_renderer] vkAllocateMemory (AHB import) failed");
        destroy_imported_image(img);
        return nullptr;
    }

    if (vkBindImageMemory(device_, img.image, img.memory, 0) != VK_SUCCESS) {
        spdlog::get("illixr")->error("[stereo_renderer] vkBindImageMemory failed");
        destroy_imported_image(img);
        return nullptr;
    }

    // VkImageView
    VkSamplerYcbcrConversionInfo view_conv{VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO};
    view_conv.conversion = img.ycbcr_conv;

    VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view_info.pNext                           = &view_conv;
    view_info.image                           = img.image;
    view_info.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format                          = VK_FORMAT_UNDEFINED;
    view_info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel   = 0;
    view_info.subresourceRange.levelCount     = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount     = 1;

    if (vkCreateImageView(device_, &view_info, nullptr, &img.image_view) != VK_SUCCESS) {
        spdlog::get("illixr")->error("[stereo_renderer] vkCreateImageView failed");
        destroy_imported_image(img);
        return nullptr;
    }

    image_cache_[hw_buffer] = std::make_unique<imported_image>(std::move(img));
    return image_cache_[hw_buffer].get();
}

stereo_renderer::imported_image* stereo_renderer::import_mv_hardware_buffer(AHardwareBuffer* hw_buffer) {
    // ── Identical to import_hardware_buffer() up to the YCbCr model ───────────
    VkAndroidHardwareBufferPropertiesANDROID       props{VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID};
    VkAndroidHardwareBufferFormatPropertiesANDROID fmt_props{
        VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID};
    props.pNext = &fmt_props;
    if (vkGetAndroidHardwareBufferPropertiesANDROID(device_, hw_buffer, &props) != VK_SUCCESS) {
        return nullptr;
    }

    VkExternalFormatANDROID ext_fmt_info{VK_STRUCTURE_TYPE_EXTERNAL_FORMAT_ANDROID};
    ext_fmt_info.externalFormat = fmt_props.externalFormat;

    VkSamplerYcbcrConversionCreateInfo ycbcr_info{VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO};
    ycbcr_info.pNext  = &ext_fmt_info;
    ycbcr_info.format = VK_FORMAT_UNDEFINED;

    // ── KEY DIFFERENCE: force RGB_IDENTITY, do NOT use suggestedYcbcrModel ──
    // The motion-vector buffer contains quantised float data, not a colour
    // video signal.  With RGB_IDENTITY the sampler performs a passthrough:
    // R ← Y_norm (Vx channel), G ← U_norm (Vy channel), B ← V_norm (Vz channel).
    // This lets motion_vec.frag dequantise directly without inverting BT.601.
    ycbcr_info.ycbcrModel                  = VK_SAMPLER_YCBCR_MODEL_CONVERSION_RGB_IDENTITY;
    ycbcr_info.ycbcrRange                  = VK_SAMPLER_YCBCR_RANGE_ITU_FULL;
    ycbcr_info.components                  = fmt_props.samplerYcbcrConversionComponents;
    ycbcr_info.xChromaOffset               = fmt_props.suggestedXChromaOffset;
    ycbcr_info.yChromaOffset               = fmt_props.suggestedYChromaOffset;
    ycbcr_info.chromaFilter                = VK_FILTER_LINEAR;
    ycbcr_info.forceExplicitReconstruction = VK_FALSE;

    // ── Identical to import_hardware_buffer() from here ───────────────────────
    imported_image img{};
    if (vkCreateSamplerYcbcrConversion(device_, &ycbcr_info, nullptr, &img.ycbcr_conv) != VK_SUCCESS) {
        return nullptr;
    }

    VkSamplerYcbcrConversionInfo conv_info{VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO};
    conv_info.conversion = img.ycbcr_conv;

    VkSamplerCreateInfo sampler_info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler_info.pNext                   = &conv_info;
    sampler_info.magFilter               = VK_FILTER_LINEAR;
    sampler_info.minFilter               = VK_FILTER_LINEAR;
    sampler_info.addressModeU            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.unnormalizedCoordinates = VK_FALSE;
    if (vkCreateSampler(device_, &sampler_info, nullptr, &img.sampler) != VK_SUCCESS) {
        vkDestroySamplerYcbcrConversion(device_, img.ycbcr_conv, nullptr);
        return nullptr;
    }

    // External image memory import — identical to import_hardware_buffer().
    VkImportAndroidHardwareBufferInfoANDROID import_info{VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID};
    import_info.buffer = hw_buffer;

    VkExternalMemoryImageCreateInfo ext_mem_img{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
    ext_mem_img.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;

    VkImageCreateInfo img_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    img_info.pNext         = &ext_mem_img;
    img_info.imageType     = VK_IMAGE_TYPE_2D;
    img_info.format        = VK_FORMAT_UNDEFINED;
    img_info.extent        = {static_cast<uint32_t>(props.allocationSize > 0 ? 1 : 1), 1, 1}; // Driver fills actual extent
    img_info.mipLevels     = 1;
    img_info.arrayLayers   = 1;
    img_info.samples       = VK_SAMPLE_COUNT_1_BIT;
    img_info.tiling        = VK_IMAGE_TILING_OPTIMAL;
    img_info.usage         = VK_IMAGE_USAGE_SAMPLED_BIT;
    img_info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    img_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkExternalFormatANDROID ext_fmt{VK_STRUCTURE_TYPE_EXTERNAL_FORMAT_ANDROID};
    ext_fmt.externalFormat = fmt_props.externalFormat;
    img_info.pNext         = &ext_fmt;
    ext_fmt.pNext          = &ext_mem_img;

    if (vkCreateImage(device_, &img_info, nullptr, &img.image) != VK_SUCCESS) {
        destroy_imported_image(img);
        return nullptr;
    }

    VkMemoryRequirements mem_reqs{};
    vkGetImageMemoryRequirements(device_, img.image, &mem_reqs);

    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = props.allocationSize;
    VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
    dedicated.image = img.image;
    dedicated.pNext = nullptr;
    alloc.pNext     = &dedicated;
    dedicated.pNext = const_cast<void*>(static_cast<const void*>(&import_info));

    uint32_t type_idx = 0;
    for (uint32_t i = 0; i < 32; i++) {
        if ((mem_reqs.memoryTypeBits >> i) & 1) {
            type_idx = i;
            break;
        }
    }
    alloc.memoryTypeIndex = type_idx;
    if (vkAllocateMemory(device_, &alloc, nullptr, &img.memory) != VK_SUCCESS) {
        destroy_imported_image(img);
        return nullptr;
    }

    VkBindImageMemoryInfo bind{VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO};
    bind.image        = img.image;
    bind.memory       = img.memory;
    bind.memoryOffset = 0;
    if (vkBindImageMemory2(device_, 1, &bind) != VK_SUCCESS) {
        destroy_imported_image(img);
        return nullptr;
    }

    VkImageViewCreateInfo        view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    VkSamplerYcbcrConversionInfo view_conv{VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO};
    view_conv.conversion       = img.ycbcr_conv;
    view_info.pNext            = &view_conv;
    view_info.image            = img.image;
    view_info.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format           = VK_FORMAT_UNDEFINED;
    view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(device_, &view_info, nullptr, &img.image_view) != VK_SUCCESS) {
        destroy_imported_image(img);
        return nullptr;
    }

    auto [it, ok] = mv_image_cache_.emplace(hw_buffer, std::make_unique<imported_image>(std::move(img)));
    if (!ok) {
        return nullptr;
    }
    return it->second.get();
}

//
// receive_frame — import the new AHardwareBuffers
//
void stereo_renderer::receive_frame(const dual_frames& frame) {
    if (!initialized_) {
        spdlog::get("illixr")->warn("stereo_renderer: receive_frame called before initialize");
        return;
    }

    if (frame.format != data_format::frame_format::hardware_buffer) {
        spdlog::get("illixr")->warn("[stereo_renderer] Received non-hardware_buffer frame, ignoring");
        return;
    }

    // Overlay metadata is tied to this exact decoded frame. Build its
    // per-eye geometry before either the combined or separate-eye path can
    // branch, so the compositor never reuses commands from another image.
    update_boba_overlay_state(frame);

    AHardwareBuffer* bufs[2] = {frame.left_eye.hw_buffer, frame.right_eye.hw_buffer};

#ifdef COMBINED_ENCODING
    if (combined_encoding_) {
        // Both eyes are encoded side-by-side in left_eye.hw_buffer.
        // Import it once; assign the same imported_image to both eyes.
        AHardwareBuffer* combined_buf = frame.left_eye.hw_buffer;
        if (combined_buf == nullptr) {
            spdlog::get("illixr")->error("[stereo_renderer] COMBINED_ENCODING: combined buffer is null");
            has_valid_frame_ = false;
            return;
        }
        imported_image* img = import_hardware_buffer(combined_buf);
        if (!img) {
            spdlog::get("illixr")->error("[stereo_renderer] COMBINED_ENCODING: import failed");
            has_valid_frame_ = false;
            return;
        }
        current_images_[0] = img;
        current_images_[1] = img;

        if (!pipeline_created_) {
            if (!create_pipeline(*img)) {
                spdlog::get("illixr")->error("[stereo_renderer] Pipeline creation failed");
                return;
            }
        }
        // Both eyes share the same image; update both descriptor sets.
        // update_descriptor_set(0, *img);
        // update_descriptor_set(1, *img);
        has_valid_frame_ = true;

        // Depth and motion vectors are still per-eye — fall through to the
        // existing depth/MV import below.
        goto handle_depth_mv;
    }
#endif // COMBINED_ENCODING

    for (int eye = 0; eye < 2; eye++) {
        if (bufs[eye] == nullptr)
            continue;
        imported_image* img = import_hardware_buffer(bufs[eye]);
        if (!img) {
            spdlog::get("illixr")->error("[stereo_renderer] Failed to import AHardwareBuffer for eye {}", eye);
            current_images_[eye] = nullptr;
            continue;
        }
        current_images_[eye] = img;

        // Lazily create the pipeline once we have a prototype image with
        // the correct external format and sampler.
        if (!pipeline_created_) {
            if (!create_pipeline(*img)) {
                spdlog::get("illixr")->error("[stereo_renderer] Pipeline creation failed");
                return;
            }
        }

        // Update descriptor set for this eye.
        // update_descriptor_set(eye, *img);
    }

    has_valid_frame_ = (current_images_[0] != nullptr && current_images_[1] != nullptr);

#ifdef COMBINED_ENCODING
handle_depth_mv:
#endif

    // current_format_ = frame.format;
    // frame_width_ = frame.width;
    // frame_height_ = frame.height; TODO:

    /*if (frame.format == frame_format::external_oes) {
        // Store texture handles (owned by decoder)
        external_textures_[0] = frame.left_eye.texture_id;
        external_textures_[1] = frame.right_eye.texture_id;

        // Copy transform matrices
        std::copy(frame.left_eye.texture_transform.begin(),
                  frame.left_eye.texture_transform.end(),
                  texture_transforms_[0].begin());
        std::copy(frame.right_eye.texture_transform.begin(),
                  frame.right_eye.texture_transform.end(),
                  texture_transforms_[1].begin());

    } else if (frame.format == frame_format::nv12) {
        // Upload NV12 data to our textures
        upload_nv12_data(0, frame.left_eye, frame.width, frame.height);
        upload_nv12_data(1, frame.right_eye, frame.width, frame.height);
    }*/

    // Depth frames
    has_depth_frame_ = false;
    if (frame.has_valid_depth()) {
        AHardwareBuffer* depth_bufs[2] = {frame.left_depth.hw_buffer, frame.right_depth.hw_buffer};
        for (int eye = 0; eye < 2; eye++) {
            if (depth_bufs[eye] == nullptr)
                continue;

            // Import into the separate depth cache so color and depth images
            // never collide (they come from different AImageReaders).
            auto it = depth_image_cache_.find(depth_bufs[eye]);
            if (it == depth_image_cache_.end()) {
                imported_image* img = import_hardware_buffer(depth_bufs[eye]);
                if (!img) {
                    spdlog::get("illixr")->error("[stereo_renderer] Failed to import depth AHardwareBuffer eye {}", eye);
                    current_depth_images_[eye] = nullptr;
                    continue;
                }
                // Move from color cache to depth cache — import_hardware_buffer
                // inserts into image_cache_; move it over.
                auto node = image_cache_.extract(depth_bufs[eye]);
                depth_image_cache_.insert(std::move(node));
                current_depth_images_[eye] = depth_image_cache_.at(depth_bufs[eye]).get();
            } else {
                current_depth_images_[eye] = it->second.get();
            }

            // if (current_depth_images_[eye]) {
            //     update_depth_descriptor_set(eye, *current_depth_images_[eye]);
            // }
        }
        has_depth_frame_ = (current_depth_images_[0] != nullptr && current_depth_images_[1] != nullptr);
        spdlog::get("illixr")->debug("stereo_renderer: Received depth");
    }

    // Motion-vector import
    if (frame.has_valid_motion_vectors()) {
        for (int eye = 0; eye < 2; eye++) {
            AHardwareBuffer* mv_buf = (eye == 0) ? frame.left_motion_vec.hw_buffer : frame.right_motion_vec.hw_buffer;

            auto it = mv_image_cache_.find(mv_buf);
            if (it == mv_image_cache_.end()) {
                imported_image* img = import_mv_hardware_buffer(mv_buf);
                if (!img) {
                    spdlog::get("illixr")->error("[stereo_renderer] Failed to import MV AHardwareBuffer eye {}", eye);
                    current_mv_images_[eye] = nullptr;
                    continue;
                }
                current_mv_images_[eye] = img;
                if (!mv_pipeline_created_) {
                    if (!create_mv_render_pass())
                        return;
                    if (!create_mv_pipeline(*img))
                        return;
                    if (!create_mv_descriptor_pool())
                        return;
                    if (!allocate_mv_command_buffers())
                        return;
                }
                // update_mv_descriptor_set(eye, *img);
            } else {
                current_mv_images_[eye] = it->second.get();
                // if (mv_pipeline_created_) {
                //     update_mv_descriptor_set(eye, it->second);
                // }
            }
        }
        has_mv_frame_ = true;
    } else {
        has_mv_frame_ = false;
    }
}

//
// render_eye — record and submit command buffer for one eye
//

bool stereo_renderer::render_eye(int eye, VkImage swapchain_image, uint32_t swapchain_width, uint32_t swapchain_height,
                                 VkSemaphore signal_semaphore) {
    if (!initialized_) {
        spdlog::get("illixr")->error("stereo_renderer: Not initialized");
        return false;
    }

    if (!pipeline_created_) {
        spdlog::get("illixr")->error("stereo_renderer: Not initialized");
        return false;
    }

    if (current_images_[eye] == nullptr) {
        return false;
    }

    // Wait for any previous submission on this eye's command buffer to finish,
    // then destroy the transient framebuffer and image view from that previous
    // submission.  These objects must not be freed while the GPU is still
    // reading them — destroying them immediately after vkQueueSubmit (before the
    // fence signals) is a spec violation that can silently corrupt rendering on
    // some drivers.
    vkWaitForFences(device_, 1, &render_fences_[eye], VK_TRUE, UINT64_MAX);
    vkResetFences(device_, 1, &render_fences_[eye]);

    // Safe to update here — fence confirms GPU is done with frame N-1
    if (current_images_[eye]) {
        update_descriptor_set(eye, *current_images_[eye]);
    }

    // Destroy the transient objects from the previous frame for this eye now
    // that the fence has confirmed the GPU is finished with them.
    if (prev_framebuffers_[eye] != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(device_, prev_framebuffers_[eye], nullptr);
        prev_framebuffers_[eye] = VK_NULL_HANDLE;
    }
    if (prev_swapchain_views_[eye] != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, prev_swapchain_views_[eye], nullptr);
        prev_swapchain_views_[eye] = VK_NULL_HANDLE;
    }

    VkCommandBuffer cmd = command_buffers_[eye];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);

    // Transition swapchain image to color attachment
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout                   = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout                   = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
    barrier.image                       = swapchain_image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask               = 0;
    barrier.dstAccessMask               = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr,
                         0, nullptr, 1, &barrier);

    // Transition decoder image to shader read
    VkImageMemoryBarrier src_barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    src_barrier.oldLayout                   = VK_IMAGE_LAYOUT_UNDEFINED;
    src_barrier.newLayout                   = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    src_barrier.srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
    src_barrier.dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
    src_barrier.image                       = current_images_[eye]->image;
    src_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    src_barrier.subresourceRange.levelCount = 1;
    src_barrier.subresourceRange.layerCount = 1;
    src_barrier.srcAccessMask               = 0;
    src_barrier.dstAccessMask               = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &src_barrier);

    // Create transient framebuffer for this swapchain image
    // We create a VkImageView for the swapchain image on the fly.
    // In production you would cache these per swapchain image index.
    VkImageViewCreateInfo sc_view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    sc_view_info.image                       = swapchain_image;
    sc_view_info.viewType                    = VK_IMAGE_VIEW_TYPE_2D;
    sc_view_info.format                      = swapchain_format_;
    sc_view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    sc_view_info.subresourceRange.levelCount = 1;
    sc_view_info.subresourceRange.layerCount = 1;
    VkImageView sc_view                      = VK_NULL_HANDLE;
    vkCreateImageView(device_, &sc_view_info, nullptr, &sc_view);

    VkFramebufferCreateInfo fb_info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fb_info.renderPass        = render_pass_;
    fb_info.attachmentCount   = 1;
    fb_info.pAttachments      = &sc_view;
    fb_info.width             = swapchain_width;
    fb_info.height            = swapchain_height;
    fb_info.layers            = 1;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    vkCreateFramebuffer(device_, &fb_info, nullptr, &framebuffer);

    // Render pass
    VkClearValue clear_value{};
    clear_value.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

    VkRenderPassBeginInfo rp_begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp_begin.renderPass        = render_pass_;
    rp_begin.framebuffer       = framebuffer;
    rp_begin.renderArea.extent = {swapchain_width, swapchain_height};
    rp_begin.clearValueCount   = 1;
    rp_begin.pClearValues      = &clear_value;
    vkCmdBeginRenderPass(cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_, 0, 1, &descriptor_sets_[eye], 0, nullptr);

    // Push crop scale constants (and u_offset under COMBINED_ENCODING).
#ifdef COMBINED_ENCODING
    // crop_scale_x covers one eye's half of the combined buffer (0.5 of full width
    // after padding correction). u_offset shifts eye 1 to the right half.
    const float u_offset     = combined_encoding_ ? (eye == 0 ? 0.0f : 0.5f) : 0.0f;
    float       push_data[3] = {crop_scale_x_ * (combined_encoding_ ? 0.5f : 1.0f), crop_scale_y_, u_offset};
    vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push_data), push_data);
#else
    float push_data[2] = {crop_scale_x_, crop_scale_y_};
    vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push_data), push_data);
#endif

    VkViewport viewport{};
    viewport.width    = static_cast<float>(swapchain_width);
    viewport.height   = static_cast<float>(swapchain_height);
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{{0, 0}, {swapchain_width, swapchain_height}};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Three vertices generate a full-screen triangle (no vertex buffer).
    vkCmdDraw(cmd, 3, 1, 0, 0);

    // Compose Boba's view-dependent vectors and optional bitmap card at the
    // Quest swapchain resolution, after the decoded base image.
    record_boba_overlays(cmd, eye);

    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);

    // Submit
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &cmd;
    if (signal_semaphore != VK_NULL_HANDLE) {
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores    = &signal_semaphore;
    }
    vkQueueSubmit(queue_, 1, &submit, render_fences_[eye]);

    // Stash the transient objects for destruction at the top of the NEXT call
    // to render_eye() for this eye, after the fence has signalled.
    prev_framebuffers_[eye]    = framebuffer;
    prev_swapchain_views_[eye] = sc_view;

    return true;
}

//
// render_eye_depth — write decoded depth into OpenXR depth swapchain image
//

bool stereo_renderer::render_eye_depth(int eye, VkImage depth_swapchain_image, VkFormat depth_format, uint32_t swapchain_width,
                                       uint32_t swapchain_height) {
    if (!has_depth_frame_ || current_depth_images_[eye] == nullptr)
        return false;

    // Lazily build the depth pipeline on first call (needs depth_format).
    if (!depth_pipeline_created_) {
        if (!create_depth_render_pass(depth_format))
            return false;
        if (!create_depth_descriptor_pool())
            return false;
        if (!allocate_depth_command_buffers())
            return false;
        if (!create_depth_pipeline(*current_depth_images_[eye], depth_format))
            return false;
    }

    // Wait for previous depth submission on this eye.
    vkWaitForFences(device_, 1, &depth_fences_[eye], VK_TRUE, UINT64_MAX);
    vkResetFences(device_, 1, &depth_fences_[eye]);
    if (current_depth_images_[eye])
        update_depth_descriptor_set(eye, *current_depth_images_[eye]);
    // Destroy transient objects from the previous depth submission for this eye.
    if (prev_depth_framebuffers_[eye] != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(device_, prev_depth_framebuffers_[eye], nullptr);
        prev_depth_framebuffers_[eye] = VK_NULL_HANDLE;
    }
    if (prev_depth_views_[eye] != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, prev_depth_views_[eye], nullptr);
        prev_depth_views_[eye] = VK_NULL_HANDLE;
    }

    VkCommandBuffer cmd = depth_command_buffers_[eye];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);

    // Transition depth swapchain image to depth attachment
    VkImageMemoryBarrier depth_barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    depth_barrier.oldLayout                   = VK_IMAGE_LAYOUT_UNDEFINED;
    depth_barrier.newLayout                   = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth_barrier.srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
    depth_barrier.dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
    depth_barrier.image                       = depth_swapchain_image;
    depth_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depth_barrier.subresourceRange.levelCount = 1;
    depth_barrier.subresourceRange.layerCount = 1;
    depth_barrier.srcAccessMask               = 0;
    depth_barrier.dstAccessMask               = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, 0, 0, nullptr,
                         0, nullptr, 1, &depth_barrier);

    // Transition decoded depth texture to shader read
    VkImageMemoryBarrier src_barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    src_barrier.oldLayout                   = VK_IMAGE_LAYOUT_UNDEFINED;
    src_barrier.newLayout                   = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    src_barrier.srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
    src_barrier.dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
    src_barrier.image                       = current_depth_images_[eye]->image;
    src_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    src_barrier.subresourceRange.levelCount = 1;
    src_barrier.subresourceRange.layerCount = 1;
    src_barrier.srcAccessMask               = 0;
    src_barrier.dstAccessMask               = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &src_barrier);

    // Create transient depth image view and framebuffer
    VkImageViewCreateInfo dv_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    dv_info.image                       = depth_swapchain_image;
    dv_info.viewType                    = VK_IMAGE_VIEW_TYPE_2D;
    dv_info.format                      = depth_format;
    dv_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    dv_info.subresourceRange.levelCount = 1;
    dv_info.subresourceRange.layerCount = 1;
    VkImageView depth_view              = VK_NULL_HANDLE;
    vkCreateImageView(device_, &dv_info, nullptr, &depth_view);

    VkFramebufferCreateInfo fb_info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fb_info.renderPass        = depth_render_pass_;
    fb_info.attachmentCount   = 1;
    fb_info.pAttachments      = &depth_view;
    fb_info.width             = swapchain_width;
    fb_info.height            = swapchain_height;
    fb_info.layers            = 1;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    vkCreateFramebuffer(device_, &fb_info, nullptr, &framebuffer);

    // Depth render pass
    VkClearValue clear_depth{};
    clear_depth.depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo rp_begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp_begin.renderPass        = depth_render_pass_;
    rp_begin.framebuffer       = framebuffer;
    rp_begin.renderArea.extent = {swapchain_width, swapchain_height};
    rp_begin.clearValueCount   = 1;
    rp_begin.pClearValues      = &clear_depth;
    vkCmdBeginRenderPass(cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, depth_pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, depth_pipeline_layout_, 0, 1, &depth_descriptor_sets_[eye], 0,
                            nullptr);

    float push_data[2] = {crop_scale_x_, crop_scale_y_};
    vkCmdPushConstants(cmd, depth_pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push_data), push_data);

    VkViewport viewport{};
    viewport.width    = static_cast<float>(swapchain_width);
    viewport.height   = static_cast<float>(swapchain_height);
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{{0, 0}, {swapchain_width, swapchain_height}};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &cmd;
    vkQueueSubmit(queue_, 1, &submit, depth_fences_[eye]);

    prev_depth_framebuffers_[eye] = framebuffer;
    prev_depth_views_[eye]        = depth_view;

    return true;
}

bool stereo_renderer::update_depth_descriptor_set(int eye, const imported_image& img) {
    if (depth_descriptor_sets_[eye] == VK_NULL_HANDLE)
        return false;

    VkDescriptorImageInfo img_info{};
    img_info.sampler     = img.sampler;
    img_info.imageView   = img.image_view;
    img_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet          = depth_descriptor_sets_[eye];
    write.dstBinding      = 0;
    write.descriptorCount = 1;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo      = &img_info;

    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    return true;
}

//
// Depth render pass, descriptor pool, pipeline, command buffers
//

bool stereo_renderer::create_depth_render_pass(VkFormat depth_format) {
    // Depth-only render pass: no colour attachment, one depth attachment.
    VkAttachmentDescription depth_attachment{};
    depth_attachment.format         = depth_format;
    depth_attachment.samples        = VK_SAMPLE_COUNT_1_BIT;
    depth_attachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_attachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    depth_attachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    depth_attachment.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depth_ref{};
    depth_ref.attachment = 0;
    depth_ref.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.pDepthStencilAttachment = &depth_ref;

    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    dep.dstStageMask  = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = 0;
    dep.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rp_info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rp_info.attachmentCount = 1;
    rp_info.pAttachments    = &depth_attachment;
    rp_info.subpassCount    = 1;
    rp_info.pSubpasses      = &subpass;
    rp_info.dependencyCount = 1;
    rp_info.pDependencies   = &dep;

    VK_CHECK(vkCreateRenderPass(device_, &rp_info, nullptr, &depth_render_pass_));
    return true;
}

bool stereo_renderer::create_depth_descriptor_pool() {
    // Pool for 2 combined-image-sampler descriptors (one per eye).
    VkDescriptorPoolSize pool_size{};
    pool_size.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_size.descriptorCount = 2;

    VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool_info.maxSets       = 2;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes    = &pool_size;
    VK_CHECK(vkCreateDescriptorPool(device_, &pool_info, nullptr, &depth_descriptor_pool_));
    return true;
}

bool stereo_renderer::allocate_depth_command_buffers() {
    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.commandPool        = command_pool_; // reuse the same pool
    alloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 2;
    VK_CHECK(vkAllocateCommandBuffers(device_, &alloc, depth_command_buffers_.data()));
    return true;
}

bool stereo_renderer::create_depth_pipeline(const imported_image& prototype, VkFormat depth_format) {
    // Descriptor set layout
    // Immutable sampler required for YCbCr (same constraint as colour pipeline).
    VkSamplerYcbcrConversionInfo ycbcr_info{VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO};
    ycbcr_info.conversion = prototype.ycbcr_conv;

    VkSamplerCreateInfo samp_info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samp_info.pNext                   = &ycbcr_info;
    samp_info.magFilter               = VK_FILTER_LINEAR;
    samp_info.minFilter               = VK_FILTER_LINEAR;
    samp_info.addressModeU            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samp_info.addressModeV            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samp_info.addressModeW            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samp_info.unnormalizedCoordinates = VK_FALSE;

    VkSampler depth_immutable_sampler = VK_NULL_HANDLE;
    if (vkCreateSampler(device_, &samp_info, nullptr, &depth_immutable_sampler) != VK_SUCCESS) {
        return false;
    }

    VkDescriptorSetLayoutBinding binding{};
    binding.binding            = 0;
    binding.descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount    = 1;
    binding.stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT;
    binding.pImmutableSamplers = &depth_immutable_sampler;

    VkDescriptorSetLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout_info.bindingCount = 1;
    layout_info.pBindings    = &binding;
    if (vkCreateDescriptorSetLayout(device_, &layout_info, nullptr, &depth_desc_set_layout_) != VK_SUCCESS) {
        vkDestroySampler(device_, depth_immutable_sampler, nullptr);
        return false;
    }
    // Transfer ownership to the member so the sampler outlives the layout and
    // pipeline.  The Vulkan spec requires the immutable sampler to remain valid
    // until the descriptor set layout is destroyed (cleanup() handles this).
    depth_immutable_sampler_ = depth_immutable_sampler;

    // Allocate depth descriptor sets (one per eye)
    std::array<VkDescriptorSetLayout, 2> layouts = {depth_desc_set_layout_, depth_desc_set_layout_};
    VkDescriptorSetAllocateInfo          alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc.descriptorPool     = depth_descriptor_pool_;
    alloc.descriptorSetCount = 2;
    alloc.pSetLayouts        = layouts.data();
    VK_CHECK(vkAllocateDescriptorSets(device_, &alloc, depth_descriptor_sets_.data()));

    // Depth fences
    VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (int i = 0; i < 2; i++) {
        if (depth_fences_[i] == VK_NULL_HANDLE) {
            VK_CHECK(vkCreateFence(device_, &fence_info, nullptr, &depth_fences_[i]));
        }
    }

    // Pipeline layout (push constants: 2 floats for crop scale)
    VkPushConstantRange pc{};
    pc.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pc.size       = sizeof(float) * 2;

    VkPipelineLayoutCreateInfo pl_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl_info.setLayoutCount         = 1;
    pl_info.pSetLayouts            = &depth_desc_set_layout_;
    pl_info.pushConstantRangeCount = 1;
    pl_info.pPushConstantRanges    = &pc;
    VK_CHECK(vkCreatePipelineLayout(device_, &pl_info, nullptr, &depth_pipeline_layout_));

    // Shaders
    VkShaderModule vert_mod = create_shader_module(device_, color_vert_spv, sizeof(color_vert_spv) / sizeof(uint32_t));
    VkShaderModule frag_mod = create_shader_module(device_, depth_frag_spv, sizeof(depth_frag_spv) / sizeof(uint32_t));
    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType                           = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage                           = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module                          = vert_mod;
    stages[0].pName                           = "main";
    stages[1].sType                           = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage                           = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module                          = frag_mod;
    stages[1].pName                           = "main";

    // Fixed-function state
    VkPipelineVertexInputStateCreateInfo   vertex_input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo input_asm{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    input_asm.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewport_state{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode    = VK_CULL_MODE_NONE;
    raster.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Depth pipeline: write depth values written by gl_FragDepth.
    VkPipelineDepthStencilStateCreateInfo depth_stencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth_stencil.depthTestEnable  = VK_FALSE; // we write our own depth, no test
    depth_stencil.depthWriteEnable = VK_TRUE;
    depth_stencil.depthCompareOp   = VK_COMPARE_OP_ALWAYS;

    // No colour blend state — depth-only render pass has no colour attachment.
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 0; // no colour attachments

    VkDynamicState                   dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates    = dynamic_states;

    VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gp.stageCount          = 2;
    gp.pStages             = stages;
    gp.pVertexInputState   = &vertex_input;
    gp.pInputAssemblyState = &input_asm;
    gp.pViewportState      = &viewport_state;
    gp.pRasterizationState = &raster;
    gp.pMultisampleState   = &ms;
    gp.pDepthStencilState  = &depth_stencil;
    gp.pColorBlendState    = &blend;
    gp.pDynamicState       = &dynamic;
    gp.layout              = depth_pipeline_layout_;
    gp.renderPass          = depth_render_pass_;

    VkResult result = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &depth_pipeline_);
    vkDestroyShaderModule(device_, vert_mod, nullptr);
    vkDestroyShaderModule(device_, frag_mod, nullptr);
    if (result != VK_SUCCESS) {
        spdlog::get("illixr")->error("[stereo_renderer] Depth pipeline creation failed: {}", static_cast<int>(result));
        return false;
    }

    depth_pipeline_created_ = true;
    spdlog::get("illixr")->info("[stereo_renderer] Depth pipeline created");
    return true;
}

bool stereo_renderer::update_descriptor_set(int eye, const imported_image& img) {
    if (descriptor_sets_[eye] == VK_NULL_HANDLE)
        return false;

    VkDescriptorImageInfo img_info{};
    img_info.sampler     = img.sampler;
    img_info.imageView   = img.image_view;
    img_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet          = descriptor_sets_[eye];
    write.dstBinding      = 0;
    write.descriptorCount = 1;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo      = &img_info;

    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    return true;
}

void stereo_renderer::cleanup() {
    if (device_ == VK_NULL_HANDLE)
        return;

    vkDeviceWaitIdle(device_);
    // Destroy any transient per-frame objects that were stashed for deferred
    // destruction (they are now safe to destroy after vkDeviceWaitIdle).
    for (int i = 0; i < 2; i++) {
        if (prev_framebuffers_[i] != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device_, prev_framebuffers_[i], nullptr);
            prev_framebuffers_[i] = VK_NULL_HANDLE;
        }
        if (prev_swapchain_views_[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(device_, prev_swapchain_views_[i], nullptr);
            prev_swapchain_views_[i] = VK_NULL_HANDLE;
        }
        if (prev_depth_framebuffers_[i] != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device_, prev_depth_framebuffers_[i], nullptr);
            prev_depth_framebuffers_[i] = VK_NULL_HANDLE;
        }
        if (prev_depth_views_[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(device_, prev_depth_views_[i], nullptr);
            prev_depth_views_[i] = VK_NULL_HANDLE;
        }
    }

    // Destroy cached color imported images
    for (auto& [key, img] : image_cache_) {
        destroy_imported_image(*img);
    }
    image_cache_.clear();

    // Destroy cached depth imported images
    for (auto& [key, img] : depth_image_cache_) {
        destroy_imported_image(*img);
    }
    depth_image_cache_.clear();

    for (int i = 0; i < 2; i++) {
        if (render_fences_[i] != VK_NULL_HANDLE) {
            vkDestroyFence(device_, render_fences_[i], nullptr);
            render_fences_[i] = VK_NULL_HANDLE;
        }
        if (depth_fences_[i] != VK_NULL_HANDLE) {
            vkDestroyFence(device_, depth_fences_[i], nullptr);
            depth_fences_[i] = VK_NULL_HANDLE;
        }
    }

    for (int i = 0; i < 2; ++i) {
        if (overlay_vertex_mapped_[i] != nullptr) {
            vkUnmapMemory(device_, overlay_vertex_memories_[i]);
            overlay_vertex_mapped_[i] = nullptr;
        }
        if (overlay_vertex_buffers_[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_, overlay_vertex_buffers_[i], nullptr);
            overlay_vertex_buffers_[i] = VK_NULL_HANDLE;
        }
        if (overlay_vertex_memories_[i] != VK_NULL_HANDLE) {
            vkFreeMemory(device_, overlay_vertex_memories_[i], nullptr);
            overlay_vertex_memories_[i] = VK_NULL_HANDLE;
        }
        if (modal_vertex_mapped_[i] != nullptr) {
            vkUnmapMemory(device_, modal_vertex_memories_[i]);
            modal_vertex_mapped_[i] = nullptr;
        }
        if (modal_vertex_buffers_[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_, modal_vertex_buffers_[i], nullptr);
            modal_vertex_buffers_[i] = VK_NULL_HANDLE;
        }
        if (modal_vertex_memories_[i] != VK_NULL_HANDLE) {
            vkFreeMemory(device_, modal_vertex_memories_[i], nullptr);
            modal_vertex_memories_[i] = VK_NULL_HANDLE;
        }
        overlay_vertices_[i].clear();
        modal_vertex_counts_[i] = 0;
    }

    if (overlay_pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, overlay_pipeline_, nullptr);
        overlay_pipeline_ = VK_NULL_HANDLE;
    }
    if (overlay_pipeline_layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, overlay_pipeline_layout_, nullptr);
        overlay_pipeline_layout_ = VK_NULL_HANDLE;
    }
    if (modal_pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, modal_pipeline_, nullptr);
        modal_pipeline_ = VK_NULL_HANDLE;
    }
    if (modal_pipeline_layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, modal_pipeline_layout_, nullptr);
        modal_pipeline_layout_ = VK_NULL_HANDLE;
    }
    if (modal_descriptor_pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, modal_descriptor_pool_, nullptr);
        modal_descriptor_pool_ = VK_NULL_HANDLE;
        modal_descriptor_set_  = VK_NULL_HANDLE;
    }
    if (modal_desc_set_layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, modal_desc_set_layout_, nullptr);
        modal_desc_set_layout_ = VK_NULL_HANDLE;
    }
    destroy_modal_texture();
    if (modal_sampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(device_, modal_sampler_, nullptr);
        modal_sampler_ = VK_NULL_HANDLE;
    }
    active_modal_         = {};
    render_boba_overlays_ = false;

    if (command_pool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device_, command_pool_, nullptr);
        command_pool_ = VK_NULL_HANDLE;
    }

    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
    if (pipeline_layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
        pipeline_layout_ = VK_NULL_HANDLE;
    }
    if (render_pass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device_, render_pass_, nullptr);
        render_pass_ = VK_NULL_HANDLE;
    }
    if (descriptor_pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);
        descriptor_pool_ = VK_NULL_HANDLE;
    }
    if (desc_set_layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, desc_set_layout_, nullptr);
        desc_set_layout_ = VK_NULL_HANDLE;
    }

    // Depth pipeline
    if (depth_pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, depth_pipeline_, nullptr);
        depth_pipeline_ = VK_NULL_HANDLE;
    }
    if (depth_pipeline_layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, depth_pipeline_layout_, nullptr);
        depth_pipeline_layout_ = VK_NULL_HANDLE;
    }
    if (depth_render_pass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device_, depth_render_pass_, nullptr);
        depth_render_pass_ = VK_NULL_HANDLE;
    }
    if (depth_descriptor_pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, depth_descriptor_pool_, nullptr);
        depth_descriptor_pool_ = VK_NULL_HANDLE;
    }
    if (depth_desc_set_layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, depth_desc_set_layout_, nullptr);
        depth_desc_set_layout_ = VK_NULL_HANDLE;
    }
    // Must be destroyed AFTER depth_desc_set_layout_ since the layout holds a
    // reference to this sampler handle (pImmutableSamplers).
    if (depth_immutable_sampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(device_, depth_immutable_sampler_, nullptr);
        depth_immutable_sampler_ = VK_NULL_HANDLE;
    }

    // Motion-vector pipeline
    for (int i = 0; i < 2; i++) {
        if (prev_mv_framebuffers_[i] != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device_, prev_mv_framebuffers_[i], nullptr);
            prev_mv_framebuffers_[i] = VK_NULL_HANDLE;
        }
        if (prev_mv_swapchain_views_[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(device_, prev_mv_swapchain_views_[i], nullptr);
            prev_mv_swapchain_views_[i] = VK_NULL_HANDLE;
        }
        if (mv_fences_[i] != VK_NULL_HANDLE) {
            vkDestroyFence(device_, mv_fences_[i], nullptr);
            mv_fences_[i] = VK_NULL_HANDLE;
        }
    }

    for (auto& [key, img] : mv_image_cache_) {
        destroy_imported_image(*img);
    }
    mv_image_cache_.clear();

    if (mv_pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, mv_pipeline_, nullptr);
        mv_pipeline_ = VK_NULL_HANDLE;
    }
    if (mv_pipeline_layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, mv_pipeline_layout_, nullptr);
        mv_pipeline_layout_ = VK_NULL_HANDLE;
    }
    if (mv_render_pass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device_, mv_render_pass_, nullptr);
        mv_render_pass_ = VK_NULL_HANDLE;
    }
    if (mv_descriptor_pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, mv_descriptor_pool_, nullptr);
        mv_descriptor_pool_ = VK_NULL_HANDLE;
    }
    if (mv_desc_set_layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, mv_desc_set_layout_, nullptr);
        mv_desc_set_layout_ = VK_NULL_HANDLE;
    }
    // Must be destroyed AFTER mv_desc_set_layout_.
    if (mv_immutable_sampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(device_, mv_immutable_sampler_, nullptr);
        mv_immutable_sampler_ = VK_NULL_HANDLE;
    }
    mv_pipeline_created_ = false;
    has_mv_frame_        = false;

    initialized_            = false;
    has_valid_frame_        = false;
    pipeline_created_       = false;
    depth_pipeline_created_ = false;
    has_depth_frame_        = false;
}

void stereo_renderer::wait_idle() {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
    }
}

//
// Motion-vector render pass
//
bool stereo_renderer::create_mv_render_pass() {
    // Single R16G16B16A16_SFLOAT colour attachment — no depth, no stencil.
    VkAttachmentDescription color_att{};
    color_att.format         = VK_FORMAT_R16G16B16A16_SFLOAT;
    color_att.samples        = VK_SAMPLE_COUNT_1_BIT;
    color_att.loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_att.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    color_att.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_att.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    color_att.finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference color_ref{};
    color_ref.attachment = 0;
    color_ref.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &color_ref;

    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rp.attachmentCount = 1;
    rp.pAttachments    = &color_att;
    rp.subpassCount    = 1;
    rp.pSubpasses      = &subpass;
    rp.dependencyCount = 1;
    rp.pDependencies   = &dep;

    VK_CHECK(vkCreateRenderPass(device_, &rp, nullptr, &mv_render_pass_));
    spdlog::get("illixr")->info("[stereo_renderer] MV render pass created");
    return true;
}

//
// Motion-vector graphics pipeline
//
bool stereo_renderer::create_mv_pipeline(const imported_image& prototype) {
    // Immutable YCbCr sampler
    // The pipeline's immutable sampler MUST use the same VkSamplerYcbcrConversion
    // as the imported image's view (Vulkan spec requirement for external-format
    // images).  import_mv_hardware_buffer() created that conversion with
    // VK_SAMPLER_YCBCR_MODEL_CONVERSION_RGB_IDENTITY so that the motion-vector
    // channels pass through as (R=Y_norm, G=U_norm, B=V_norm) without a BT.601
    // colour matrix being applied to the quantised velocity data.
    // Use prototype.ycbcr_conv directly — exactly as create_depth_pipeline() does.

    VkSamplerYcbcrConversionInfo conv_info{VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO};
    conv_info.conversion = prototype.ycbcr_conv;

    VkSamplerCreateInfo sampler_ci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler_ci.pNext                   = &conv_info;
    sampler_ci.magFilter               = VK_FILTER_LINEAR;
    sampler_ci.minFilter               = VK_FILTER_LINEAR;
    sampler_ci.addressModeU            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_ci.addressModeV            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_ci.addressModeW            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_ci.unnormalizedCoordinates = VK_FALSE;

    VK_CHECK(vkCreateSampler(device_, &sampler_ci, nullptr, &mv_immutable_sampler_));

    // Descriptor set layout
    VkDescriptorSetLayoutBinding binding{};
    binding.binding            = 0;
    binding.descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount    = 1;
    binding.stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT;
    binding.pImmutableSamplers = &mv_immutable_sampler_;

    VkDescriptorSetLayoutCreateInfo layout_ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout_ci.bindingCount = 1;
    layout_ci.pBindings    = &binding;

    VK_CHECK(vkCreateDescriptorSetLayout(device_, &layout_ci, nullptr, &mv_desc_set_layout_));

    // Pipeline layout (push constants for crop scale)
    VkPushConstantRange pc_range{};
    pc_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pc_range.offset     = 0;
    pc_range.size       = 2 * sizeof(float); // crop_scale_x, crop_scale_y

    VkPipelineLayoutCreateInfo pl_ci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl_ci.setLayoutCount         = 1;
    pl_ci.pSetLayouts            = &mv_desc_set_layout_;
    pl_ci.pushConstantRangeCount = 1;
    pl_ci.pPushConstantRanges    = &pc_range;

    VK_CHECK(vkCreatePipelineLayout(device_, &pl_ci, nullptr, &mv_pipeline_layout_));

    // Shader stages
    // Reuse the color vertex shader (full-screen triangle + crop push constants)
    // and use the new motion-vector fragment shader.
    VkShaderModule vert_mod = create_shader_module(device_, color_vert_spv, sizeof(color_vert_spv) / sizeof(uint32_t));
    VkShaderModule frag_mod =
        create_shader_module(device_, motion_vec_frag_spv, sizeof(motion_vec_frag_spv) / sizeof(uint32_t));

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert_mod;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag_mod;
    stages[1].pName  = "main";

    // Fixed-function state
    VkPipelineVertexInputStateCreateInfo   vertex_input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo input_asm{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    input_asm.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewport_state{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode    = VK_CULL_MODE_NONE;
    raster.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Single R16G16B16A16_SFLOAT colour attachment — standard alpha blend.
    VkPipelineColorBlendAttachmentState blend_att{};
    blend_att.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1;
    blend.pAttachments    = &blend_att;

    VkDynamicState                   dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates    = dynamic_states;

    VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gp.stageCount          = 2;
    gp.pStages             = stages;
    gp.pVertexInputState   = &vertex_input;
    gp.pInputAssemblyState = &input_asm;
    gp.pViewportState      = &viewport_state;
    gp.pRasterizationState = &raster;
    gp.pMultisampleState   = &ms;
    gp.pDepthStencilState  = nullptr; // no depth attachment
    gp.pColorBlendState    = &blend;
    gp.pDynamicState       = &dynamic;
    gp.layout              = mv_pipeline_layout_;
    gp.renderPass          = mv_render_pass_;

    VkResult result = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &mv_pipeline_);
    vkDestroyShaderModule(device_, vert_mod, nullptr);
    vkDestroyShaderModule(device_, frag_mod, nullptr);
    if (result != VK_SUCCESS) {
        spdlog::get("illixr")->error("[stereo_renderer] MV pipeline creation failed: {}", static_cast<int>(result));
        return false;
    }

    // Fence start state: signalled (nothing in flight).
    VkFenceCreateInfo fence_ci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fence_ci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (int i = 0; i < 2; i++) {
        VK_CHECK(vkCreateFence(device_, &fence_ci, nullptr, &mv_fences_[i]));
    }

    mv_pipeline_created_ = true;
    spdlog::get("illixr")->info("[stereo_renderer] Motion-vector pipeline created");
    return true;
}

//
bool stereo_renderer::create_mv_descriptor_pool() {
    VkDescriptorPoolSize pool_size{};
    pool_size.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_size.descriptorCount = 2; // one per eye

    VkDescriptorPoolCreateInfo pool_ci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool_ci.maxSets       = 2;
    pool_ci.poolSizeCount = 1;
    pool_ci.pPoolSizes    = &pool_size;

    VK_CHECK(vkCreateDescriptorPool(device_, &pool_ci, nullptr, &mv_descriptor_pool_));

    std::array<VkDescriptorSetLayout, 2> layouts{mv_desc_set_layout_, mv_desc_set_layout_};
    VkDescriptorSetAllocateInfo          alloc_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc_info.descriptorPool     = mv_descriptor_pool_;
    alloc_info.descriptorSetCount = 2;
    alloc_info.pSetLayouts        = layouts.data();
    VK_CHECK(vkAllocateDescriptorSets(device_, &alloc_info, mv_descriptor_sets_.data()));
    return true;
}

//
bool stereo_renderer::allocate_mv_command_buffers() {
    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.commandPool        = command_pool_; // reuse the existing pool
    alloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 2;
    VK_CHECK(vkAllocateCommandBuffers(device_, &alloc, mv_command_buffers_.data()));
    return true;
}

//
bool stereo_renderer::update_mv_descriptor_set(int eye, const imported_image& img) {
    if (mv_descriptor_sets_[eye] == VK_NULL_HANDLE)
        return false;

    VkDescriptorImageInfo img_info{};
    img_info.sampler     = img.sampler;
    img_info.imageView   = img.image_view;
    img_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet          = mv_descriptor_sets_[eye];
    write.dstBinding      = 0;
    write.descriptorCount = 1;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo      = &img_info;

    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    return true;
}

//
// Motion-vector render
//
bool stereo_renderer::render_eye_motion_vec(int eye, VkImage mv_swapchain_image, uint32_t swapchain_width,
                                            uint32_t swapchain_height) {
    if (!initialized_ || !mv_pipeline_created_) {
        spdlog::get("illixr")->warn("[stereo_renderer] render_eye_motion_vec: not ready (eye {})", eye);
        return false;
    }
    if (!has_mv_frame_ || current_mv_images_[eye] == nullptr) {
        spdlog::get("illixr")->debug("[stereo_renderer] render_eye_motion_vec: no MV frame (eye {})", eye);
        return false;
    }

    // Wait for the previous MV submission on this eye to finish.
    vkWaitForFences(device_, 1, &mv_fences_[eye], VK_TRUE, UINT64_MAX);
    vkResetFences(device_, 1, &mv_fences_[eye]);
    if (current_mv_images_[eye])
        update_mv_descriptor_set(eye, *current_mv_images_[eye]);
    // Destroy the transient framebuffer and image view from the previous frame.
    if (prev_mv_framebuffers_[eye] != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(device_, prev_mv_framebuffers_[eye], nullptr);
        prev_mv_framebuffers_[eye] = VK_NULL_HANDLE;
    }
    if (prev_mv_swapchain_views_[eye] != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, prev_mv_swapchain_views_[eye], nullptr);
        prev_mv_swapchain_views_[eye] = VK_NULL_HANDLE;
    }

    // Create swapchain image view
    VkImageViewCreateInfo iv_ci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    iv_ci.image            = mv_swapchain_image;
    iv_ci.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    iv_ci.format           = VK_FORMAT_R16G16B16A16_SFLOAT;
    iv_ci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkImageView mv_view = VK_NULL_HANDLE;
    VK_CHECK(vkCreateImageView(device_, &iv_ci, nullptr, &mv_view));
    prev_mv_swapchain_views_[eye] = mv_view;

    // Create framebuffer
    VkFramebufferCreateInfo fb_ci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fb_ci.renderPass      = mv_render_pass_;
    fb_ci.attachmentCount = 1;
    fb_ci.pAttachments    = &mv_view;
    fb_ci.width           = swapchain_width;
    fb_ci.height          = swapchain_height;
    fb_ci.layers          = 1;

    VkFramebuffer fb = VK_NULL_HANDLE;
    VK_CHECK(vkCreateFramebuffer(device_, &fb_ci, nullptr, &fb));
    prev_mv_framebuffers_[eye] = fb;

    // Record command buffer
    VkCommandBuffer cmd = mv_command_buffers_[eye];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &begin));

    // Transition swapchain image from UNDEFINED to COLOR_ATTACHMENT_OPTIMAL.
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image               = mv_swapchain_image;
    barrier.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.srcAccessMask       = 0;
    barrier.dstAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr,
                         0, nullptr, 1, &barrier);

    // Begin render pass.
    VkRenderPassBeginInfo rp_begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp_begin.renderPass        = mv_render_pass_;
    rp_begin.framebuffer       = fb;
    rp_begin.renderArea.offset = {0, 0};
    rp_begin.renderArea.extent = {swapchain_width, swapchain_height};
    // No clear colour needed (loadOp = DONT_CARE).
    vkCmdBeginRenderPass(cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mv_pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mv_pipeline_layout_, 0, 1, &mv_descriptor_sets_[eye], 0,
                            nullptr);

    // Push crop scale (motion-vector buffers are already at their native
    // resolution with no padding, so scale = 1.0).
    float push[2] = {1.0f, 1.0f};
    vkCmdPushConstants(cmd, mv_pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), push);

    VkViewport viewport{0.0f, 0.0f, static_cast<float>(swapchain_width), static_cast<float>(swapchain_height), 0.0f, 1.0f};
    VkRect2D   scissor{{0, 0}, {swapchain_width, swapchain_height}};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdDraw(cmd, 3, 1, 0, 0);

    vkCmdEndRenderPass(cmd);

    // Transition swapchain image to SHADER_READ_ONLY_OPTIMAL for OpenXR.
    barrier.oldLayout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &barrier);

    VK_CHECK(vkEndCommandBuffer(cmd));

    // Submit
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &cmd;

    VkResult result = vkQueueSubmit(queue_, 1, &submit, mv_fences_[eye]);
    if (result != VK_SUCCESS) {
        spdlog::get("illixr")->error("[stereo_renderer] MV render submit failed eye {}: {}", eye, static_cast<int>(result));
        return false;
    }
    return true;
}
