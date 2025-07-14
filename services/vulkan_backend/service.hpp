#pragma once

#include <queue>

#include <vulkan/vulkan.h>

#include <illixr/plugin.hpp>
#include <illixr/graphics_backend.hpp>

namespace ILLIXR {

class vulkan_backend_impl : public graphics_backend {
public:
    explicit vulkan_backend_impl(const phonebook* pb);
    virtual ~vulkan_backend_impl() override;

private:
    void create_instance();
    VkInstance instance_;

    void choose_physical_device();
    VkPhysicalDevice physical_device_;

    void create_logical_device();
    VkDevice logical_device_;

    std::queue<std::function<void()>> global_deletion_queue_;
};

class vulkan_backend : public plugin {
public:
    [[maybe_unused]] vulkan_backend(const std::string& name, phonebook* pb);
    ~vulkan_backend() override;
};

} // namespace ILLIXR