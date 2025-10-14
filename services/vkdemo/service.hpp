#pragma once

#include "illixr/data_format/pose_prediction.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"
#include "illixr/vk/display_provider.hpp"
#include "illixr/vk/render_pass.hpp"

#include <functional>
#include <stack>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/hash.hpp>
#include <map>

namespace ILLIXR {
struct vertex {
    glm::vec3 pos;
    glm::vec2 uv;

    static VkVertexInputBindingDescription get_binding_description() {
        VkVertexInputBindingDescription binding_description{
            0,                          // binding
            sizeof(vertex),             // stride
            VK_VERTEX_INPUT_RATE_VERTEX // inputRate
        };
        return binding_description;
    }

    static std::array<VkVertexInputAttributeDescription, 2> get_attribute_descriptions() {
        std::array<VkVertexInputAttributeDescription, 2> attribute_descriptions{{{
                                                                                     0,                          // location
                                                                                     0,                          // binding
                                                                                     VK_FORMAT_R32G32B32_SFLOAT, // format
                                                                                     offsetof(vertex, pos)       // offset
                                                                                 },
                                                                                 {
                                                                                     1,                       // location
                                                                                     0,                       // binding
                                                                                     VK_FORMAT_R32G32_SFLOAT, // format
                                                                                     offsetof(vertex, uv)     // offset
                                                                                 }}};

        return attribute_descriptions;
    }

    bool operator==(const vertex& other) const {
        return pos == other.pos && uv == other.uv;
    }
};

struct model {
    int                       texture_index;
    [[maybe_unused]] uint32_t vertex_offset;
    uint32_t                  index_offset;
    uint32_t                  index_count;
};

struct texture {
    VkImage       image;
    VmaAllocation image_memory;
    VkImageView   image_view;
};

class MY_EXPORT_API vkdemo : public vulkan::app {
public:
    explicit vkdemo(const phonebook* const pb);
    void initialize();
    void setup(VkRenderPass render_pass, uint32_t subpass,
               std::shared_ptr<vulkan::buffer_pool<data_format::fast_pose_type>> _) override;
    void update_uniforms(const data_format::pose_type& fp) override;
    void record_command_buffer(VkCommandBuffer command_buffer, VkFramebuffer frame_buffer, int buffer_ind, bool left) override;
    void destroy() override;

    [[maybe_unused]] bool is_external() override {
        return false;
    }

private:
    void update_uniform(const data_format::pose_type& pose, int eye);
    void bake_models();
    void create_descriptor_set_layout();
    void create_uniform_buffers();
    void create_descriptor_pool();
    void create_texture_sampler_();
    void create_descriptor_set();
    void load_texture(const std::string& path, int i);
    void image_layout_transition(VkImage image, [[maybe_unused]] VkFormat format, VkImageLayout old_layout,
                                 VkImageLayout new_layout);
    void load_model();
    void create_vertex_buffer();
    void create_index_buffer();
    void create_pipeline(VkRenderPass render_pass, uint32_t subpass);

    const std::shared_ptr<switchboard>                  switchboard_;
    const std::shared_ptr<data_format::pose_prediction> pose_prediction_;
    const std::shared_ptr<vulkan::display_provider>     display_provider_ = nullptr;
    const std::shared_ptr<const relative_clock>         clock_;

    Eigen::Matrix4f       basic_projection_[2];
    std::vector<model>    models_;
    std::vector<vertex>   vertices_;
    std::vector<uint32_t> indices_;

    std::array<std::vector<VkImageView>, 2> buffer_pool_;

    std::stack<std::function<void()>> deletion_queue_;
    VmaAllocator                      vma_allocator_{};
    VkCommandPool                     command_pool_{};
    [[maybe_unused]] VkCommandBuffer  command_buffer_{};

    VkDescriptorSetLayout          descriptor_set_layout_{};
    VkDescriptorPool               descriptor_pool_{};
    std::array<VkDescriptorSet, 2> descriptor_sets_{};

    std::array<VkBuffer, 2>          uniform_buffers_{};
    std::array<VmaAllocation, 2>     uniform_buffer_allocations_{};
    std::array<VmaAllocationInfo, 2> uniform_buffer_allocation_infos_{};

    VkBuffer vertex_buffer_{};
    VkBuffer index_buffer_{};

    VkPipelineLayout pipeline_layout_{};

    std::vector<VkDescriptorImageInfo> image_infos_;
    std::vector<texture>               textures_;
    VkSampler                          texture_sampler_{};
    std::map<uint32_t, uint32_t>       texture_map_;
};

class vkdemo_plugin : public plugin {
public:
    [[maybe_unused]] vkdemo_plugin(const std::string& name, phonebook* pb);
    void start() override;

private:
    std::shared_ptr<vkdemo> vkd_;
};

} // namespace ILLIXR
