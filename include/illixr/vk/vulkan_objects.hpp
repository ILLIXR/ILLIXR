#ifndef ILLIXR_VULKAN_OBJECTS_HPP
#define ILLIXR_VULKAN_OBJECTS_HPP
#include "illixr/vk/third_party/vk_mem_alloc.h"
#include <vulkan/vulkan.h>

namespace ILLIXR::vulkan {

typedef uint8_t image_index_t;

struct vk_image {
    VkImageCreateInfo image_info;
    VkImage image;
    VkImageView image_view;
    VmaAllocation allocation;
    VmaAllocationInfo allocation_info;
};

template <typename T>
struct buffer_pool {
    enum image_state {
        FREE,
        DECODE_IN_FLIGHT,
        DECODED,
        RENDER_IN_FLIGHT
    };

    std::vector<std::array<vk_image, 2>> image_pool;
    std::vector<std::array<vk_image, 2>> depth_image_pool;

    std::vector<image_state> image_states;
    std::vector<T> image_data;
    std::mutex image_state_mutex;

    image_index_t latest_decoded_image = -1;

    image_index_t decode_acquire_image() {
        std::lock_guard<std::mutex> lock(image_state_mutex);
        for (image_index_t i = 0; i < image_states.size(); i++) {
            if (image_states[i] == FREE) {
                image_states[i] = DECODE_IN_FLIGHT;
                return i;
            }
        }
        return -1;
    }

    /**
     * @brief Release the image after it has been decoded.
     *
     * It is the caller's responsibility to ensure that the image is in the DECODE_IN_FLIGHT state.
     * Images must be released in order. If an image is released out of order, the previous images
     * will be marked as free.
     * @param image_index The index of the image to release.
     */
    void decode_release_image(image_index_t image_index, T&& image_data) {
        std::lock_guard<std::mutex> lock(image_state_mutex);
        // Mark the previous images as free
        for (image_index_t i = 0; i < image_index; i++) {
            if (image_states[i] == DECODED) {
                image_states[i] = FREE;
            }
        }
        assert(image_states[image_index] == DECODE_IN_FLIGHT);
        image_states[image_index] = DECODED;
        this->image_data[image_index] = std::move(image_data);
        latest_decoded_image = image_index;
    }

    image_index_t render_acquire_image() {
        std::lock_guard<std::mutex> lock(image_state_mutex);
        assert(latest_decoded_image != -1);
        assert(image_states[latest_decoded_image] == DECODED);
        image_states[latest_decoded_image] = RENDER_IN_FLIGHT;
        return latest_decoded_image;
    }

    void render_release_image(image_index_t image_index) {
        std::lock_guard<std::mutex> lock(image_state_mutex);
        assert(image_states[image_index] == RENDER_IN_FLIGHT);
        if (image_index != latest_decoded_image) {
            image_states[image_index] = FREE;
        } else {
            image_states[image_index] = DECODED;
        }
    }

    buffer_pool(const std::vector<std::array<vk_image, 2>>& image_pool,
                const std::vector<std::array<vk_image, 2>>& depth_image_pool)
        : image_pool(image_pool)
        , depth_image_pool(depth_image_pool) {
        assert(image_pool.size() == depth_image_pool.size());
        image_states.resize(image_pool.size());
    }
};

}

#endif // ILLIXR_VULKAN_OBJECTS_HPP
