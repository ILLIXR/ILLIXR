#include <vector>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "../data_format.hpp"
#include "../phonebook.hpp"

using namespace ILLIXR;

class timewarp : public phonebook::service {
public:
    virtual void setup(VkRenderPass render_pass, uint32_t subpass, std::vector<VkImageView> buffer_pool) = 0;

    virtual ~timewarp() { }

    VkPipeline pipeline = VK_NULL_HANDLE;
};
