#include <cstdint>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include "third_party/VkBootstrap.h"
#include "vulkan_utils.hpp"

#include "../data_format.hpp"
#include "../phonebook.hpp"

using namespace ILLIXR;

class display_sink : public phonebook::service {
public:
    virtual ~display_sink() { }

    GLFWwindow*              window;
    VkInstance               vk_instance;
    VkSurfaceKHR             vk_surface;
    VkPhysicalDevice         vk_physical_device;
    VkDevice                 vk_device;
    VkQueue                  graphics_queue;
    VkQueue                  present_queue;
    uint32_t                 graphics_queue_family;
    uint32_t                 present_queue_family;
    VkSwapchainKHR           vk_swapchain;
    VkFormat                 swapchain_image_format;
    VkExtent2D               swapchain_extent;
    std::vector<VkImage>     swapchain_images;
    std::vector<VkImageView> swapchain_image_views;

    // optional
    VmaAllocator             vma_allocator;
};
