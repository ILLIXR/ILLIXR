#include <cstdint>
#include "third_party/VkBootstrap.h"
#include "vulkan_utils.hpp"

#include "../data_format.hpp"
#include "../phonebook.hpp"

using namespace ILLIXR;

class display_sink : public phonebook::service {
public:
    virtual ~display_sink() { }

    // required by timewarp_vk as a service
    VkInstance               vk_instance;
    VkPhysicalDevice         vk_physical_device;
    VkDevice                 vk_device;
    VkQueue                  graphics_queue;
    uint32_t                 graphics_queue_family;

    virtual void poll_window_events() { };

    // addtionally required for native display
    virtual void recreate_swapchain() { };

    void*              window;
    VkSurfaceKHR             vk_surface;
    VkQueue                  present_queue;
    uint32_t                 present_queue_family;
    VkSwapchainKHR           vk_swapchain;
    VkFormat                 swapchain_image_format;
    std::vector<VkImage>     swapchain_images;
    std::vector<VkImageView> swapchain_image_views;
    VkExtent2D               swapchain_extent;

    // optional
    VmaAllocator             vma_allocator;
    
};
