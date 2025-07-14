#include "service.hpp"

#include "vk_util.hpp"

using namespace ILLIXR;

vulkan_backend_impl::vulkan_backend_impl(const phonebook* pb) {
    create_instance();
}

void vulkan_backend_impl::create_instance() {
    VkApplicationInfo app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "ILLIXR",
        .applicationVersion = VK_MAKE_VERSION(1, 3, 0),
        .pEngineName = "ILLIXR",
        .engineVersion = VK_MAKE_VERSION(0, 0, 1),
        .apiVersion = VK_API_VERSION_1_3,
    };

    VkInstanceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app_info,
    };

    VK_ASSERT_SUCCESS(vkCreateInstance(&create_info, nullptr, &instance_));

    global_deletion_queue_.emplace([&]() {
        vkDestroyInstance(instance_, nullptr);
    });
}

vulkan_backend_impl::~vulkan_backend_impl() {
    // Clear out all Vulkan objects in the queue
    while (!global_deletion_queue_.empty()) {
        auto destructor = global_deletion_queue_.front();
        global_deletion_queue_.pop();
        destructor();
    }
}

[[maybe_unused]] vulkan_backend::vulkan_backend(const std::string& name, phonebook* pb)
    : plugin{name, pb} {
    // "pose_prediction" is a class inheriting from "phonebook::service"
    //   It is described in "pose_prediction.hpp"
    pb->register_impl<graphics_backend>(std::static_pointer_cast<graphics_backend>(std::make_shared<vulkan_backend_impl>(pb)));
#ifndef NDEBUG
    spdlog::get("illixr")->debug("[vulkan_backend] Starting Vulkan Backend");
#endif
}

vulkan_backend::~vulkan_backend() {
#ifndef NDEBUG
    spdlog::get("illixr")->debug("[vulkan_backend] Ending Vulkan Backend");
#endif
}

// This line makes the plugin importable by Spindle
PLUGIN_MAIN(vulkan_backend)