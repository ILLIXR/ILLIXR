#pragma once

#include "../data_format.hpp"
#include "../phonebook.hpp"
#include "vulkan_utils.hpp"

#include <cstdint>

using namespace ILLIXR;

namespace ILLIXR::vulkan { /**
                            * @brief A display sink is a service that can display the rendered images to the screen.
                            *
                            * @details
                            * A display sink is a service created with the necessary Vulkan resources to display the rendered
                            * images to the screen. It is created either by display_vk, a plugin that configures the Vulkan
                            * resources and swapchain, or by monado_vulkan_integration, which populate the Vulkan resources and
                            * swapchain from Monado. Previously with the GL implementation, this was not required since we were
                            * using GL and Monado was using Vulkan.
                            */

class display_provider : public phonebook::service {
public:
    ~display_provider() override = default;

    // required by timewarp_vk as a service

    VkInstance                                                               vk_instance        = VK_NULL_HANDLE;
    VkPhysicalDevice                                                         vk_physical_device = VK_NULL_HANDLE;
    VkDevice                                                                 vk_device          = VK_NULL_HANDLE;
    std::unordered_map<queue::queue_type, queue> queues;

    pthread_mutex_t monado_mutex;

    /**
     * @brief Polls window events using whatever the windowing backend is.
     */
    virtual void poll_window_events(){};

    // addtionally required for native display

    /**
     * @brief Recreates the swapchain when an outdated or nonoptimal swapchain is detected.
     */
    virtual void recreate_swapchain(){};

    VkSurfaceKHR              vk_surface   = VK_NULL_HANDLE;
    VkSwapchainKHR            vk_swapchain = VK_NULL_HANDLE;
    VkSurfaceFormatKHR        swapchain_image_format;
    std::vector<VkImage>      swapchain_images;
    std::vector<VkImageView>  swapchain_image_views;
    VkExtent2D                swapchain_extent = {display_params::width_pixels, display_params::height_pixels};

    // optional
    VmaAllocator vma_allocator;

    // for ffmpeg
    VkPhysicalDeviceVulkan13Features vk13_features;
    VkPhysicalDeviceFeatures2 features;
    std::vector<const char*>  enabled_instance_extensions;
    std::vector<const char*>  enabled_device_extensions;
};
} // namespace ILLIXR::vulkan
