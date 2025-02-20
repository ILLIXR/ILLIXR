#pragma once

#include "illixr/phonebook.hpp"
#include "illixr/pose_prediction.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"
#include "illixr/vk_util/display_sink.hpp"
#include "illixr/vk_util/render_pass.hpp"
#include "utils/hmd.hpp"

#include <stack>

namespace ILLIXR {
class timewarp_vk : public timewarp {
public:
    explicit timewarp_vk(const phonebook* pb);
    void initialize();
    void setup(VkRenderPass render_pass, uint32_t subpass, std::array<std::vector<VkImageView>, 2> buffer_pool_in,
               bool input_texture_vulkan_coordinates_in) override;
    void partial_destroy();
    void update_uniforms(const pose_type& render_pose) override;
    void record_command_buffer(VkCommandBuffer command_buffer, int buffer_ind, bool left) override;
    void destroy() override;

private:
    void        create_vertex_buffer();
    void        create_index_buffer();
    void        generate_distortion_data();
    void        create_texture_sampler();
    void        create_descriptor_set_layout();
    void        create_uniform_buffer();
    void        create_descriptor_pool();
    void        create_descriptor_sets();
    VkPipeline  create_pipeline(VkRenderPass render_pass, [[maybe_unused]] uint32_t subpass);
    void        build_timewarp(HMD::hmd_info_t& hmd_info);
    static void calculate_timewarp_transform(Eigen::Matrix4f& transform, const Eigen::Matrix4f& render_projection_matrix,
                                             const Eigen::Matrix4f& render_view_matrix, const Eigen::Matrix4f& new_view_matrix);

    const phonebook* const                 phonebook_;
    const std::shared_ptr<switchboard>     switchboard_;
    const std::shared_ptr<pose_prediction> pose_prediction_;
    bool                                   disable_warp_ = false;
    std::shared_ptr<display_sink>          display_sink_ = nullptr;
    std::mutex                             setup_mutex_;

    bool initialized_                      = false;
    bool input_texture_vulkan_coordinates_ = true;

    // Vulkan resources
    std::stack<std::function<void()>> deletion_queue_;
    VmaAllocator                      vma_allocator_{};

    std::array<std::vector<VkImageView>, 2> buffer_pool_;
    VkSampler                               fb_sampler_{};

    VkDescriptorPool                            descriptor_pool_{};
    VkDescriptorSetLayout                       descriptor_set_layout_{};
    std::array<std::vector<VkDescriptorSet>, 2> descriptor_sets_;

    VkPipelineLayout  pipeline_layout_{};
    VkBuffer          uniform_buffer_{};
    VmaAllocation     uniform_alloc_{};
    VmaAllocationInfo uniform_alloc_info_{};

    VkCommandPool                    command_pool_{};
    [[maybe_unused]] VkCommandBuffer command_buffer_{};

    VkBuffer vertex_buffer_{};
    VkBuffer index_buffer_{};

    // distortion data
    HMD::hmd_info_t hmd_info_{};

    uint32_t                         num_distortion_vertices_{};
    uint32_t                         num_distortion_indices_{};
    Eigen::Matrix4f                  basic_projection_;
    std::vector<HMD::mesh_coord3d_t> distortion_positions_;
    std::vector<HMD::uv_coord_t>     distortion_uv0_;
    std::vector<HMD::uv_coord_t>     distortion_uv1_;
    std::vector<HMD::uv_coord_t>     distortion_uv2_;

    std::vector<uint32_t> distortion_indices_;

    // metrics
    std::atomic<uint32_t> num_record_calls_{0};
    std::atomic<uint32_t> num_update_uniforms_calls_{0};

    friend class timewarp_vk_plugin;
};

class timewarp_vk_plugin : public threadloop {
public:
    [[maybe_unused]] timewarp_vk_plugin(const std::string& name, phonebook* pb);
    void        _p_one_iteration() override;
    skip_option _p_should_skip() override;

private:
    std::shared_ptr<timewarp_vk> timewarp_;

    int64_t last_print_ = 0;
};

} // namespace ILLIXR
