#pragma once

#include "illixr/data_format/pose_prediction.hpp"
#include "illixr/hmd.hpp"
#include "illixr/switchboard.hpp"
// #include "illixr/threadloop.hpp"
#include "illixr/vk/display_provider.hpp"
#include "illixr/vk/render_pass.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/hash.hpp>
#include <mutex>
#include <stack>

namespace ILLIXR {

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
    explicit openwarp_vk(const phonebook* pb);
    void initialize();
    void setup(VkRenderPass render_pass, uint32_t subpass,
               std::shared_ptr<vulkan::buffer_pool<data_format::fast_pose_type>> buffer_pool_,
               bool                                                              input_texture_external_) override;
    void partial_destroy();
    void update_uniforms(const data_format::pose_type& render_pose, bool left) override;
    void record_command_buffer(VkCommandBuffer commandBuffer, VkFramebuffer framebuffer, int buffer_ind, bool left) override;
    bool is_external() override;
    void destroy() override;

private:
    void                   create_offscreen_images();
    void                   create_vertex_buffers();
    void                   create_index_buffers();
    void                   generate_distortion_data();
    void                   generate_openwarp_mesh(size_t width, size_t height);
    void                   create_texture_sampler();
    void                   create_descriptor_set_layouts();
    void                   create_uniform_buffers();
    void                   create_descriptor_pool();
    void                   create_descriptor_sets();
    void                   create_openwarp_pipeline();
    VkPipeline             create_distortion_correction_pipeline(VkRenderPass render_pass, [[maybe_unused]] uint32_t subpass);
    static Eigen::Matrix4f create_camera_matrix(const data_format::pose_type& pose, int eye);
    static Eigen::Matrix4f calculate_distortion_transform(const Eigen::Matrix4f& projection_matrix);

    const phonebook* const             phonebook_;
    const std::shared_ptr<switchboard> switchboard_;
    const std::shared_ptr<relative_clock> relative_clock_;

    const std::shared_ptr<data_format::pose_prediction> pose_prediction_;

    bool                                      disable_warp_     = false;
    std::shared_ptr<vulkan::display_provider> display_provider_ = nullptr;
    std::mutex                                setup_mutex_;

    bool initialized_            = false;
    bool input_texture_external_ = false;

    bool using_godot_         = false;
    bool offloaded_rendering_ = false;

    uint64_t frame_count_ = 0;

    // Vulkan resources
    std::stack<std::function<void()>> deletion_queue_;
    VmaAllocator                      vma_allocator_{};

    // Note that each frame occupies 2 elements in the buffer pool:
    // i for the image itself, and i + 1 for the depth image.
    size_t swapchain_width_{};
    size_t swapchain_height_{};

    std::shared_ptr<vulkan::buffer_pool<data_format::fast_pose_type>> buffer_pool_;

    VkSampler fb_sampler_{};

    VkDescriptorPool descriptor_pool_{};
    VkCommandPool    command_pool_{};
    VkCommandBuffer  command_buffer_{};

    // offscreen image used as an intermediate render target
    std::array<VkImage, 2>       offscreen_images_{};
    std::array<VkImageView, 2>   offscreen_image_views_{};
    std::array<VmaAllocation, 2> offscreen_image_allocs_{};

    std::array<VkImage, 2>       offscreen_depths_{};
    std::array<VkImageView, 2>   offscreen_depth_views_{};
    std::array<VmaAllocation, 2> offscreen_depth_allocs_{};

    std::array<VkFramebuffer, 2> offscreen_framebuffers_{};

    // openwarp mesh
    VkPipelineLayout ow_pipeline_layout_{};

    VkBuffer          ow_matrices_uniform_buffer_{};
    VmaAllocation     ow_matrices_uniform_alloc_{};
    VmaAllocationInfo ow_matrices_uniform_alloc_info_{};

    VkDescriptorSetLayout                       ow_descriptor_set_layout_{};
    std::array<std::vector<VkDescriptorSet>, 2> ow_descriptor_sets_;

    uint32_t                    num_openwarp_vertices_{};
    uint32_t                    num_openwarp_indices_{};
    std::vector<OpenWarpVertex> openwarp_vertices_;
    std::vector<uint32_t>       openwarp_indices_;
    size_t                      openwarp_width_  = 0;
    size_t                      openwarp_height_ = 0;

    VkBuffer      ow_vertex_buffer_{};
    VmaAllocation ow_vertex_alloc_{};
    VkBuffer      ow_index_buffer_{};
    VmaAllocation ow_index_alloc_{};

    VkRenderPass openwarp_render_pass_{};
    VkPipeline   openwarp_pipeline_ = VK_NULL_HANDLE;

    // distortion data
    HMD::hmd_info_t hmd_info_{};
    Eigen::Matrix4f basic_projection_[2];
    Eigen::Matrix4f inverse_projection_[2];

    VkPipelineLayout  dp_pipeline_layout_{};
    VkBuffer          dp_uniform_buffer_{};
    VmaAllocation     dp_uniform_alloc_{};
    VmaAllocationInfo dp_uniform_alloc_info_{};

    VkDescriptorSetLayout                       dp_descriptor_set_layout_{};
    std::array<std::vector<VkDescriptorSet>, 2> dp_descriptor_sets_;

    uint32_t                                num_distortion_vertices_{};
    uint32_t                                num_distortion_indices_{};
    std::vector<DistortionCorrectionVertex> distortion_vertices_;
    std::vector<uint32_t>                   distortion_indices_;

    VkRenderPass  distortion_correction_render_pass_{};
    VkBuffer      dc_vertex_buffer_{};
    VmaAllocation dc_vertex_alloc_{};
    VkBuffer      dc_index_buffer_{};
    VmaAllocation dc_index_alloc_{};

    // metrics
    std::atomic<uint32_t> num_record_calls_{0};
    std::atomic<uint32_t> num_update_uniforms_calls_{0};

    friend class openwarp_vk_plugin;
};

} // namespace ILLIXR
