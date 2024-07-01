#pragma once

#include "../data_format.hpp"
#include "../phonebook.hpp"
#include "third_party/VkBootstrap.h"
#include "vulkan_utils.hpp"

#include <cstdint>

using namespace ILLIXR;

/**
 * @brief A display sink is a service that can display the rendered images to the screen.
 *
 * @details
 * A display sink is a service created with the necessary Vulkan resources to display the rendered images to the screen.
 * It is created either by display_vk, a plugin that configures the Vulkan resources and swapchain,
 * or by monado_vulkan_integration, which populate the Vulkan resources and swapchain from Monado.
 * Previously with the GL implementation, this was not required since we were using GL and Monado was using Vulkan.
 */
class display_sink : public phonebook::service {
public:
    ~display_sink() override = default;

    // required by timewarp_vk as a service

    VkInstance       vk_instance;
    VkPhysicalDevice vk_physical_device;
    VkDevice         vk_device;
    VkQueue          graphics_queue;
    uint32_t         graphics_queue_family;

    /**
     * @brief Polls window events using whatever the windowing backend is.
     */
    virtual void poll_window_events() {};

    // addtionally required for native display

    /**
     * @brief Recreates the swapchain when an outdated or nonoptimal swapchain is detected.
     */
    virtual void recreate_swapchain() {};

    void*                    window;
    VkSurfaceKHR             vk_surface;
    VkQueue                  present_queue;
    uint32_t                 present_queue_family;
    VkSwapchainKHR           vk_swapchain;
    VkFormat                 swapchain_image_format;
    std::vector<VkImage>     swapchain_images;
    std::vector<VkImageView> swapchain_image_views;
    VkExtent2D               swapchain_extent;

    // optional
    VmaAllocator vma_allocator;
};
