#pragma once

#include "illixr/data_format/pose_prediction.hpp"
#include "illixr/hmd.hpp"
#include "illixr/vk/display_provider.hpp"
#include "illixr/vk/render_pass.hpp"

#include <stack>
#include <vulkan/vulkan.h>

namespace ILLIXR {

class timewarp_vk : public vulkan::timewarp {
public:
    explicit timewarp_vk(const phonebook* pb);
    void initialize();
    void setup(VkRenderPass render_pass, uint32_t subpass,
               std::shared_ptr<vulkan::buffer_pool<data_format::fast_pose_type>> buffer_pool,
               bool                                                              input_texture_vulkan_coordinates_in) override;
    void partial_destroy();
    void update_uniforms(const data_format::pose_type& render_pose) override;
    void record_command_buffer(VkCommandBuffer commandBuffer, VkFramebuffer framebuffer, int buffer_ind, bool left) override;
    void destroy() override;

private:
    void create_vertex_buffer();
    void create_index_buffer();
    void generate_distortion_data();
    void create_texture_sampler();
    void create_descriptor_set_layout();
    void create_uniform_buffer();
    void create_descriptor_pool();
    void create_descriptor_sets();

    bool is_external() override {
        return false;
    }

    VkPipeline  create_pipeline(VkRenderPass render_pass, [[maybe_unused]] uint32_t subpass);
    void        build_timewarp(HMD::hmd_info_t& hmd_info);
    static void calculate_timewarp_transform(Eigen::Matrix4f& transform, const Eigen::Matrix4f& render_projection_matrix,
                                             const Eigen::Matrix4f& render_view_matrix, const Eigen::Matrix4f& new_view_matrix);

    const phonebook* const                                      phonebook_;
    const std::shared_ptr<switchboard>                          switchboard_;
    const std::shared_ptr<data_format::pose_prediction>         pose_prediction_;
    switchboard::reader<switchboard::event_wrapper<time_point>> vsync_;
    bool                                                        disable_warp_     = false;
    std::shared_ptr<vulkan::display_provider>                   display_provider_ = nullptr;
    std::mutex                                                  setup_mutex_;

    bool initialized_            = false;
    bool input_texture_external_ = false;

    // Vulkan resources
    std::stack<std::function<void()>> deletion_queue_;
    VmaAllocator                      vma_allocator_{};

    uint64_t frame_count_ = 0;

    size_t swapchain_width_  = 0;
    size_t swapchain_height_ = 0;

    std::shared_ptr<vulkan::buffer_pool<data_format::fast_pose_type>> buffer_pool_;

    VkSampler                                   fb_sampler_{};
    bool                                        clamp_edge_ = false;
    VkDescriptorPool                            descriptor_pool_{};
    VkDescriptorSetLayout                       descriptor_set_layout_{};
    std::array<std::vector<VkDescriptorSet>, 2> descriptor_sets_;

    VkRenderPass timewarp_render_pass_{};

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
    Eigen::Matrix4f                  basic_projection_[2];
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

} // namespace ILLIXR
