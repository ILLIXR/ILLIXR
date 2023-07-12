#include <vector>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "../data_format.hpp"
#include "../phonebook.hpp"

using namespace ILLIXR;

class render_pass : public phonebook::service {
public:

    virtual void update_uniforms(const pose_type render_pose) = 0;

    virtual ~render_pass() { }

    VkPipeline pipeline = VK_NULL_HANDLE;
};

class timewarp : public render_pass { 
public:
    virtual void setup(VkRenderPass render_pass, uint32_t subpass, std::array<std::vector<VkImageView>, 2> buffer_pool, bool input_texture_vulkan_coordinates = true) = 0;

    virtual void record_command_buffer(VkCommandBuffer commandBuffer, int buffer_ind, bool left) = 0;

};

class app : public render_pass { 
public:
    virtual void setup(VkRenderPass render_pass, uint32_t subpass) = 0;

    virtual void record_command_buffer(VkCommandBuffer commandBuffer, int eye) = 0;

};