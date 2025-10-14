#pragma once

#if defined(_WIN32) || defined(_WIN64)
#include <vma/vk_mem_alloc.h>
#else

#endif
#include <vulkan/vulkan.h>

namespace ILLIXR::vulkan {

typedef int8_t image_index_t;

struct vk_image {
    VkImageCreateInfo               image_info;
    VkExternalMemoryImageCreateInfo export_image_info;
    VkImage                         image;
    VkImageView                     image_view;
    VmaAllocation                   allocation;
    VmaAllocationInfo               allocation_info;
    int                             fd;
};

template<typename T>
struct buffer_pool {
    enum image_state { FREE, SRC_IN_FLIGHT, AVAILABLE, POST_PROCESSING_IN_FLIGHT };

    std::vector<std::array<vk_image, 2>> image_pool;
    std::vector<std::array<vk_image, 2>> depth_image_pool;

    std::vector<image_state> image_states{};
    std::vector<T>           image_data{};
    std::mutex               image_state_mutex{};

    image_index_t latest_decoded_image = -1;

    explicit buffer_pool(const std::vector<std::array<vk_image, 2>>& image_pool,
                         const std::vector<std::array<vk_image, 2>>& depth_image_pool)
        : image_pool(image_pool)
        , depth_image_pool(depth_image_pool) {
        image_states.resize(image_pool.size());
        image_data.resize(image_pool.size());
    }

    image_index_t src_acquire_image() {
        std::lock_guard<std::mutex> lock(image_state_mutex);
        for (image_index_t i = 0; i < image_states.size(); i++) {
            if (image_states[i] == FREE) {
                image_states[i] = SRC_IN_FLIGHT;
                // std::cout << "Src acquire image " << (int) i << std::endl;
                return i;
            }
        }
        assert(false && "No free images in buffer pool");
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
    void src_release_image(image_index_t image_index, T&& data) {
        std::lock_guard<std::mutex> lock(image_state_mutex);
        // Mark the previous images as free
        for (image_index_t i = 0; i < image_states.size(); i++) {
            if (image_states[i] == AVAILABLE) {
                image_states[i] = FREE;
            }
        }
        assert(image_states[image_index] == SRC_IN_FLIGHT);
        image_states[image_index]     = AVAILABLE;
        this->image_data[image_index] = std::move(data);
        latest_decoded_image          = image_index;

        // std::cout << "Src release image " << (int) image_index << std::endl;
    }

    std::pair<image_index_t, T> post_processing_acquire_image(image_index_t last_image_index = -1) {
        std::lock_guard<std::mutex> lock(image_state_mutex);
        if (latest_decoded_image == -1) {
            return {-1, {}};
        }
        if (latest_decoded_image == last_image_index) {
            return {-1, {}};
        }
        assert(latest_decoded_image != -1);
        assert(image_states[latest_decoded_image] == AVAILABLE);
        image_states[latest_decoded_image] = POST_PROCESSING_IN_FLIGHT;

        // std::cout << "Post processing acquire image " << (int) latest_decoded_image << std::endl;
        return {latest_decoded_image, image_data[latest_decoded_image]};
    }

    void post_processing_release_image(image_index_t image_index) {
        std::lock_guard<std::mutex> lock(image_state_mutex);
        assert(image_states[image_index] == POST_PROCESSING_IN_FLIGHT);
        if (image_index != latest_decoded_image) {
            image_states[image_index] = FREE;
        } else {
            image_states[image_index] = AVAILABLE;
        }

        // std::cout << "Post processing release image " << (int) image_index << std::endl;
    }
};

} // namespace ILLIXR::vulkan
