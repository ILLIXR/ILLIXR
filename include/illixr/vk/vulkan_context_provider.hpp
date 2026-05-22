#pragma once

#include "illixr/data_format/vulkan_context.hpp"
#include "illixr/phonebook.hpp"

namespace ILLIXR::vk {
class vulkan_context_provider : public phonebook::service {
public:
    data_format::vulkan_device_context get_context() const {
        return context_;
    }

protected:
    void set_context(data_format::vulkan_device_context& context) {
        context_ = context;
    }

    void set_context(VkInstance instance, VkPhysicalDevice p_device, VkDevice device, VkQueue queue, uint32_t family) {
        context_.instance        = instance;
        context_.physical_device = p_device;
        context_.device          = device;
        context_.queue           = queue;
        context_.queue_family    = family;
    }

private:
    data_format::vulkan_device_context context_;
};
} // namespace ILLIXR::vk