#include <vector>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "../data_format.hpp"
#include "../phonebook.hpp"

using namespace ILLIXR;

class render_pass : public phonebook::service {
public:

    virtual ~render_pass() { }

    VkPipeline pipeline = VK_NULL_HANDLE;
};

class timewarp : public render_pass { 

    virtual void setup(VkRenderPass render_pass, uint32_t subpass, std::array<std::vector<VkImageView>, 2> buffer_pool) = 0;

    virtual void update_uniforms(const fast_pose_type render_pose) = 0;

    virtual void record_command_buffer(VkCommandBuffer commandBuffer, int buffer_ind) = 0;

};

class app : public render_pass { 

    virtual void setup(VkRenderPass render_pass, uint32_t subpass) = 0;

    virtual void record_command_buffer(VkCommandBuffer commandBuffer, int eye) = 0;

};