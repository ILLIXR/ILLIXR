#pragma once

#include "display/glfw_extended.hpp"
#include "display/headless.hpp"
#include "display/x11_direct.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"
#include "illixr/vk/display_provider.hpp"
#include "illixr/vk/third_party/vk_mem_alloc.h"

#include <set>
#include <thread>
#include <vulkan/vulkan.h>

using namespace ILLIXR;

class display_vk : public vulkan::display_provider {
public:
    explicit display_vk(const phonebook* const pb)
        : switchboard_{pb->lookup_impl<switchboard>()}
        , clock_{pb->lookup_impl<relative_clock>()} { }

    ~display_vk() override {
        running_ = false;
        if (main_thread_.joinable()) {
            main_thread_.join();
        }
        cleanup();
    }

    void start(std::set<const char*> instance_extensions, std::set<const char*> device_extensions) {
        auto manual_device_selection = switchboard_->get_env_char("ILLIXR_VULKAN_SELECT_GPU");
        selected_gpu_                = manual_device_selection ? std::stoi(manual_device_selection) : -1;

        // ILLIXR_DISPLAY_MODE defaults to GLFW if not specified.
        const char* env_var = switchboard_->get_env_char("ILLIXR_DISPLAY_MODE");
        if (env_var == nullptr) {
            spdlog::get("illixr")->info("[vulkan_display] No display mode specified, defaulting to GLFW");
            env_var = "glfw";
        }
        if (!strcmp(env_var, "glfw")) {
            spdlog::get("illixr")->info("[vulkan_display] Selected GLFW for display backend");
            backend_type_ = display::display_backend::GLFW;
        } else if (!strcmp(env_var, "headless")) {
            spdlog::get("illixr")->info("[vulkan_display] Selected headless for display backend");
            backend_type_ = display::display_backend::HEADLESS;
        } else if (!strcmp(env_var, "x11_direct")) {
            spdlog::get("illixr")->info("[vulkan_display] Selected X11 direct mode for display backend");
            backend_type_ = display::display_backend::X11_DIRECT;
            direct_mode_  = true;
        } else {
            throw std::runtime_error("Invalid display mode: " + std::string(env_var));
        }

        setup(std::move(instance_extensions), std::move(device_extensions));

        if (backend_type_ == display::display_backend::GLFW /* || backend_type_ == display::display_backend::X11_DIRECT*/) {
            main_thread_ = std::thread(&display_vk::main_loop, this);
            while (!ready_) {
                // yield
                std::this_thread::yield();
            }
        }
    }

    /**
     * @brief This function sets up the GLFW and Vulkan environments. See display_provider::setup().
     */
    void setup(std::set<const char*> instance_extensions, std::set<const char*> device_extensions) {
        if (backend_type_ == display::display_backend::GLFW) {
            backend_ = std::make_shared<display::glfw_extended>();
        } else if (backend_type_ == display::display_backend::X11_DIRECT) {
            backend_ = std::make_shared<display::x11_direct>(
                clock_, switchboard_->get_writer<switchboard::event_wrapper<time_point>>("vsync_estimate"));
        } else {
            backend_ = std::make_shared<display::headless>();
        }

        if (backend_type_ != display::display_backend::HEADLESS) {
            required_device_extensions_.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        }

        create_vk_instance(std::move(instance_extensions));
        if (backend_type_ == display::display_backend::GLFW) {
            backend_->setup_display(switchboard_, vk_instance_, nullptr);
            vk_surface_ = backend_->create_surface();
            select_physical_device();
        } else {
            select_physical_device();
            backend_->setup_display(switchboard_, vk_instance_, vk_physical_device_);
            vk_surface_ = backend_->create_surface();
        }

        auto backend_device_extensions = backend_->get_required_device_extensions();
        device_extensions.insert(backend_device_extensions.begin(), backend_device_extensions.end());
        create_logical_device(std::move(device_extensions));

        if (backend_type_ != display::display_backend::HEADLESS) {
            create_swapchain();
        }
    }

    /**
     * @brief This function recreates the Vulkan swapchain. See display_provider::recreate_swapchain().
     */
    void recreate_swapchain() override {
        if (backend_type_ == display::display_backend::HEADLESS) {
            throw std::runtime_error("Cannot recreate swapchain in headless mode!");
        }

        vkDeviceWaitIdle(vk_device_);

        destroy_swapchain();

        create_swapchain();
    }

    /**
     * @brief This function polls GLFW events. See display_provider::poll_window_events().
     */
    void poll_window_events() override {
        should_poll_ = true;
    }

private:
    void create_vk_instance(const std::set<const char*>& instance_extensions) {
        // Enable validation layers if ILLIXR_VULKAN_VALIDATION_LAYERS is set to true.
        bool enable_validation_layers = switchboard_->get_env_bool("ILLIXR_VULKAN_VALIDATION_LAYERS");
        if (enable_validation_layers)
            spdlog::get("illixr")->info("[vulkan_display] Vulkan validation layers enabled");

        VkApplicationInfo app_info{};
        app_info.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app_info.pApplicationName   = "ILLIXR Vulkan Display";
        app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        app_info.pEngineName        = "ILLIXR";
        app_info.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
        app_info.apiVersion         = VK_API_VERSION_1_2;

        auto                     backend_required_instance_extensions = backend_->get_required_instance_extensions();
        std::vector<const char*> backend_required_instance_extensions_vec(backend_required_instance_extensions.begin(),
                                                                          backend_required_instance_extensions.end());
        if (enable_validation_layers) {
            backend_required_instance_extensions_vec.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }
        backend_required_instance_extensions_vec.insert(backend_required_instance_extensions_vec.end(),
                                                        instance_extensions.begin(), instance_extensions.end());

        this->enabled_instance_extensions_ = backend_required_instance_extensions_vec;

        VkInstanceCreateInfo create_info{};
        create_info.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        create_info.pApplicationInfo        = &app_info;
        create_info.enabledExtensionCount   = static_cast<uint32_t>(backend_required_instance_extensions_vec.size());
        create_info.ppEnabledExtensionNames = backend_required_instance_extensions_vec.data();

        // print enabled instance extensions
        spdlog::get("illixr")->info("[vulkan_display] Enabled instance extensions:");
        for (const auto& extension : backend_required_instance_extensions_vec) {
            spdlog::get("illixr")->info("\t {}", extension);
        }

        // enable validation layers
        std::vector<const char*> validation_layers = {"VK_LAYER_KHRONOS_validation"};
        if (switchboard_->get_env_bool("ILLIXR_COMPARE_IMAGES")) {
            validation_layers.push_back("VK_LAYER_LUNARG_screenshot");
        }

        if (enable_validation_layers) {
            create_info.enabledLayerCount   = static_cast<uint32_t>(validation_layers.size());
            create_info.ppEnabledLayerNames = validation_layers.data();

            // debug messenger
            VkDebugUtilsMessengerCreateInfoEXT debug_messenger_create_info{};
            debug_messenger_create_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            debug_messenger_create_info.messageSeverity =
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            debug_messenger_create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            debug_messenger_create_info.pfnUserCallback =
                [](VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType,
                   const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData) -> VkBool32 {
                (void) pUserData;
                // convert severity flag to string
                const char* severity = "???";
                if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) {
                    severity = "VERBOSE";
                } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
                    severity = "INFO";
                } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
                    severity = "WARNING";
                } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
                    severity = "ERROR";
                }

                // convert message type flag to string
                const char* type = "???";
                if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT) {
                    type = "GENERAL";
                } else if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) {
                    type = "VALIDATION";
                } else if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) {
                    type = "PERFORMANCE";
                }
                spdlog::get("illixr")->warn("[display_vk] [{}: {}] {}", severity, type, pCallbackData->pMessage);
                return VK_FALSE;
            };

            create_info.pNext = &debug_messenger_create_info;
        } else {
            create_info.enabledLayerCount = 0;
        }

        if (vkCreateInstance(&create_info, nullptr, &vk_instance_) != VK_SUCCESS) {
            ILLIXR::abort("Failed to create Vulkan instance!");
        }
    }

    bool is_physical_device_suitable(VkPhysicalDevice const& physical_device) {
        VkPhysicalDeviceProperties device_properties;
        vkGetPhysicalDeviceProperties(physical_device, &device_properties);

        VkPhysicalDeviceFeatures device_features;
        vkGetPhysicalDeviceFeatures(physical_device, &device_features);

        // check if the device supports the extensions we need
        std::set<std::string> unmet_extensions(required_device_extensions_.begin(), required_device_extensions_.end());

        uint32_t extension_count;
        vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &extension_count, nullptr);
        std::vector<VkExtensionProperties> available_extensions(extension_count);
        vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &extension_count, available_extensions.data());

        for (const auto& extension : available_extensions) {
            unmet_extensions.erase(extension.extensionName);
        }

        if (!unmet_extensions.empty()) {
            return false;
        }

        if (backend_type_ == display::display_backend::GLFW) {
            // check if the device supports the swapchain we need
            auto swapchain_details = vulkan::query_swapchain_details(physical_device, vk_surface_);
            if (swapchain_details.formats.empty() || swapchain_details.present_modes.empty()) {
                return false;
            }
        }

        return true;
    }

    void select_physical_device() {
        uint32_t device_count = 0;
        vkEnumeratePhysicalDevices(vk_instance_, &device_count, nullptr);

        if (device_count == 0) {
            ILLIXR::abort("No Vulkan devices found!");
        }

        std::vector<VkPhysicalDevice> devices(device_count);
        vkEnumeratePhysicalDevices(vk_instance_, &device_count, devices.data());

        std::vector<VkPhysicalDevice> suitable_devices;
        std::vector<VkPhysicalDevice> unsuitable_devices;
        for (const auto& device : devices) {
            if (is_physical_device_suitable(device)) {
                suitable_devices.push_back(device);
            } else {
                unsuitable_devices.push_back(device);
            }
        }

        spdlog::get("illixr")->info("[vulkan_display] Found {} Vulkan devices", device_count);
        int index = 0;
        for (const auto& device : suitable_devices) {
            VkPhysicalDeviceProperties device_properties;
            vkGetPhysicalDeviceProperties(device, &device_properties);
            spdlog::get("illixr")->info("\t[{}] {}", index, device_properties.deviceName);
            index++;
        }
        if (!unsuitable_devices.empty()) {
            spdlog::get("illixr")->info("[vulkan_display] Found {} unsuitable Vulkan devices", unsuitable_devices.size());
            for (const auto& device : unsuitable_devices) {
                VkPhysicalDeviceProperties device_properties;
                vkGetPhysicalDeviceProperties(device, &device_properties);
                spdlog::get("illixr")->info("\t{}", device_properties.deviceName);
            }
        }

        if (selected_gpu_ == -1) {
            // select the first suitable device
            vk_physical_device_ = suitable_devices[0];
        } else {
            // select the device specified by the user
            vk_physical_device_ = suitable_devices[selected_gpu_];
        }

        VkPhysicalDeviceProperties device_properties;
        vkGetPhysicalDeviceProperties(vk_physical_device_, &device_properties);
        spdlog::get("illixr")->info("[vulkan_display] Selected device: {}", device_properties.deviceName);
    }

    void create_logical_device(const std::set<const char*>& device_extensions) {
        vulkan::queue_families indices =
            vulkan::find_queue_families(vk_physical_device_, vk_surface_, backend_type_ == display::display_backend::HEADLESS);

        std::vector<VkDeviceQueueCreateInfo> queue_create_infos;
        std::set<uint32_t>                   unique_queue_families = {indices.graphics_family.value()};

        if (indices.present_family.has_value()) {
            unique_queue_families.insert(indices.present_family.value());
        }

        if (indices.has_compression()) {
            unique_queue_families.insert(indices.encode_family.value());
            unique_queue_families.insert(indices.decode_family.value());
        }

        if (indices.dedicated_transfer.has_value()) {
            unique_queue_families.insert(indices.dedicated_transfer.value());
        }

        if (indices.compute_family.has_value()) {
            unique_queue_families.insert(indices.compute_family.value());
        }

        float queue_priority = 1.0f;
        for (uint32_t queue_family : unique_queue_families) {
            VkDeviceQueueCreateInfo queue_create_info{};
            queue_create_info.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queue_create_info.queueFamilyIndex = queue_family;
            queue_create_info.queueCount       = 1;
            queue_create_info.pQueuePriorities = &queue_priority;
            queue_create_infos.push_back(queue_create_info);
        }

        VkPhysicalDeviceFeatures device_features{};
        device_features.samplerAnisotropy = VK_TRUE;

        VkPhysicalDeviceSynchronization2FeaturesKHR synchronization_2_features = {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES, nullptr, true};

        VkPhysicalDeviceTimelineSemaphoreFeatures timeline_semaphore_features{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES,
            &synchronization_2_features, // pNext
            VK_TRUE                      // timelineSemaphore
        };

        features_ = VkPhysicalDeviceFeatures2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &timeline_semaphore_features,
                                              device_features};

        required_device_extensions_.insert(required_device_extensions_.end(), device_extensions.begin(),
                                           device_extensions.end());

        this->enabled_device_extensions_ = required_device_extensions_;

        VkDeviceCreateInfo create_info{};
        create_info.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        create_info.pQueueCreateInfos       = queue_create_infos.data();
        create_info.queueCreateInfoCount    = static_cast<uint32_t>(queue_create_infos.size());
        create_info.enabledExtensionCount   = static_cast<uint32_t>(required_device_extensions_.size());
        create_info.ppEnabledExtensionNames = required_device_extensions_.data();
        create_info.pNext                   = &features_;

        // print enabled device extensions
        spdlog::get("illixr")->info("[vulkan_display] Enabled instance extensions:");
        for (const auto& extension : required_device_extensions_) {
            spdlog::get("illixr")->info("\t {}", extension);
        }

        VK_ASSERT_SUCCESS(vkCreateDevice(vk_physical_device_, &create_info, nullptr, &vk_device_));

        vulkan::queue graphics_queue{};
        vkGetDeviceQueue(vk_device_, indices.graphics_family.value(), 0, &graphics_queue.vk_queue);
        graphics_queue.family        = indices.graphics_family.value();
        graphics_queue.type          = vulkan::queue::GRAPHICS;
        graphics_queue.mutex         = std::make_shared<std::mutex>();
        queues_[graphics_queue.type] = graphics_queue;

        if (indices.present_family.has_value()) {
            vulkan::queue present_queue{};
            vkGetDeviceQueue(vk_device_, indices.present_family.value(), 0, &present_queue.vk_queue);
            present_queue.family        = indices.present_family.value();
            present_queue.type          = vulkan::queue::PRESENT;
            present_queue.mutex         = std::make_shared<std::mutex>();
            queues_[present_queue.type] = present_queue;
        }

        if (indices.has_compression()) {
            vulkan::queue encode_queue{};
            vkGetDeviceQueue(vk_device_, indices.encode_family.value(), 0, &encode_queue.vk_queue);
            encode_queue.family        = indices.encode_family.value();
            encode_queue.type          = vulkan::queue::ENCODE;
            encode_queue.mutex         = std::make_shared<std::mutex>();
            queues_[encode_queue.type] = encode_queue;

            vulkan::queue decode_queue{};
            vkGetDeviceQueue(vk_device_, indices.decode_family.value(), 0, &decode_queue.vk_queue);
            decode_queue.family        = indices.decode_family.value();
            decode_queue.type          = vulkan::queue::DECODE;
            decode_queue.mutex         = std::make_shared<std::mutex>();
            queues_[decode_queue.type] = decode_queue;
        }

        if (indices.compute_family.has_value()) {
            vulkan::queue compute_queue{};
            vkGetDeviceQueue(vk_device_, indices.compute_family.value(), 0, &compute_queue.vk_queue);
            compute_queue.family        = indices.compute_family.value();
            compute_queue.type          = vulkan::queue::COMPUTE;
            compute_queue.mutex         = std::make_shared<std::mutex>();
            queues_[compute_queue.type] = compute_queue;
        }

        if (indices.dedicated_transfer.has_value()) {
            vulkan::queue transfer_queue{};
            vkGetDeviceQueue(vk_device_, indices.dedicated_transfer.value(), 0, &transfer_queue.vk_queue);
            transfer_queue.family        = indices.dedicated_transfer.value();
            transfer_queue.type          = vulkan::queue::DEDICATED_TRANSFER;
            transfer_queue.mutex         = std::make_shared<std::mutex>();
            queues_[transfer_queue.type] = transfer_queue;
        }

        vma_allocator_ = vulkan::create_vma_allocator(vk_instance_, vk_physical_device_, vk_device_);
    }

    /**
     * @brief Sets up the Vulkan environment.
     *
     * This function initializes the Vulkan instance, selects the physical device, creates the Vulkan device,
     * gets the graphics and present queues_, creates the swapchain, and sets up the VMA allocator.
     *
     * @throws runtime_error If any of the Vulkan setup steps fail.
     */
    void create_swapchain() {
        // create surface
        vulkan::swapchain_details swapchain_details = vulkan::query_swapchain_details(vk_physical_device_, vk_surface_);

        // choose surface format
        for (const auto& available_format : swapchain_details.formats) {
            if (available_format.format == VK_FORMAT_B8G8R8A8_SRGB &&
                available_format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                spdlog::get("illixr")->info("[vulkan_display] Using VK_FORMAT_B8G8R8A8_SRGB");
                swapchain_image_format_ = available_format;
                break;
            } else if (available_format.format == VK_FORMAT_B8G8R8A8_UNORM &&
                       available_format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                spdlog::get("illixr")->info("[vulkan_display] Using VK_FORMAT_B8G8R8A8_UNORM (direct mode)");
                swapchain_image_format_ = available_format;
                break;
            }
        }

        // choose present mode
        VkPresentModeKHR swapchain_present_mode = VK_PRESENT_MODE_FIFO_KHR;
        for (const auto& available_present_mode : swapchain_details.present_modes) {
            if (available_present_mode == VK_PRESENT_MODE_MAILBOX_KHR) {
                swapchain_present_mode = available_present_mode;
                break;
            }
        }

        // choose swapchain extent
        if (swapchain_details.capabilities.currentExtent.width != UINT32_MAX) {
            swapchain_extent_ = swapchain_details.capabilities.currentExtent;
        } else if (std::dynamic_pointer_cast<display::glfw_extended>(backend_) != nullptr) {
            auto fb_size             = std::dynamic_pointer_cast<display::glfw_extended>(backend_)->get_framebuffer_size();
            swapchain_extent_.width  = std::clamp(fb_size.first, swapchain_details.capabilities.minImageExtent.width,
                                                  swapchain_details.capabilities.maxImageExtent.width);
            swapchain_extent_.height = std::clamp(fb_size.second, swapchain_details.capabilities.minImageExtent.height,
                                                  swapchain_details.capabilities.maxImageExtent.height);
        }

        uint32_t image_count = std::max(swapchain_details.capabilities.minImageCount, 2u); // double buffering
        if (swapchain_details.capabilities.maxImageCount > 0 && image_count > swapchain_details.capabilities.maxImageCount) {
            image_count = swapchain_details.capabilities.maxImageCount;
        }

        VkSwapchainCreateInfoKHR create_info{}; //{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        create_info.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        create_info.surface          = vk_surface_;
        create_info.minImageCount    = image_count;
        create_info.imageFormat      = swapchain_image_format_.format;
        create_info.imageColorSpace  = swapchain_image_format_.colorSpace;
        create_info.imageExtent      = swapchain_extent_;
        create_info.imageArrayLayers = 1;
        create_info.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        vulkan::queue_families indices                = vulkan::find_queue_families(vk_physical_device_, vk_surface_);
        uint32_t               queue_family_indices[] = {indices.graphics_family.value(), indices.present_family.value()};

        if (indices.graphics_family != indices.present_family) {
            create_info.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
            create_info.queueFamilyIndexCount = 2;
            create_info.pQueueFamilyIndices   = queue_family_indices;
        } else {
            create_info.imageSharingMode      = VK_SHARING_MODE_EXCLUSIVE;
            create_info.queueFamilyIndexCount = 0;
            create_info.pQueueFamilyIndices   = nullptr;
        }

        create_info.preTransform   = swapchain_details.capabilities.currentTransform;
        create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        create_info.presentMode    = swapchain_present_mode;
        create_info.clipped        = VK_TRUE;
        create_info.oldSwapchain   = VK_NULL_HANDLE;

        auto create_shared_swapchains =
            (PFN_vkCreateSharedSwapchainsKHR) vkGetInstanceProcAddr(vk_instance_, "vkCreateSharedSwapchainsKHR");

        if (vkCreateSwapchainKHR(vk_device_, &create_info, nullptr, &vk_swapchain_) != VK_SUCCESS) {
            ILLIXR::abort("Failed to create Vulkan swapchain!");
        }

        // get swapchain images
        vkGetSwapchainImagesKHR(vk_device_, vk_swapchain_, &image_count, nullptr);
        swapchain_images_.resize(image_count);
        vkGetSwapchainImagesKHR(vk_device_, vk_swapchain_, &image_count, swapchain_images_.data());

        swapchain_image_views_.resize(swapchain_images_.size());
        for (size_t i = 0; i < swapchain_images_.size(); i++) {
            swapchain_image_views_[i] = vulkan::create_image_view(vk_device_, swapchain_images_[i],
                                                                  swapchain_image_format_.format, VK_IMAGE_ASPECT_COLOR_BIT);
        }
    }

    void destroy_swapchain() {
        for (auto& image_view : swapchain_image_views_) {
            vkDestroyImageView(vk_device_, image_view, nullptr);
        }
        vkDestroySwapchainKHR(vk_device_, vk_swapchain_, nullptr);
    }

    void cleanup() {
        vkDeviceWaitIdle(vk_device_);
        destroy_swapchain();

        vmaDestroyAllocator(vma_allocator_);

        vkDestroyDevice(vk_device_, nullptr);

        if (!direct_mode_) {
            vkDestroySurfaceKHR(vk_instance_, vk_surface_, nullptr);
        }

        vkDestroyInstance(vk_instance_, nullptr);

        backend_->cleanup();
    }

    void main_loop() {
        ready_ = true;

        while (running_) {
            if (!direct_mode_) {
                if (should_poll_.exchange(false)) {
                    auto glfw_backend = std::dynamic_pointer_cast<display::glfw_extended>(backend_);
                    glfw_backend->poll_window_events();
                }
            } else {
                auto x11_backend = std::dynamic_pointer_cast<display::x11_direct>(backend_);
                if (!x11_backend->display_timings_event_registered_) {
                    x11_backend->register_display_timings_event(vk_device_);
                } else {
                    x11_backend->tick();
                }
            }
        }
    }

private:
    std::vector<const char*> required_device_extensions_ = {VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME};

    std::thread       main_thread_;
    std::atomic<bool> ready_{false};
    std::atomic<bool> running_{true};

    display::display_backend::display_backend_type backend_type_;
    bool                                           direct_mode_{false};
    int                                            selected_gpu_{-1};

    std::shared_ptr<display::display_backend> backend_;

    const std::shared_ptr<switchboard> switchboard_;

    std::atomic<bool> should_poll_{true};

    std::shared_ptr<relative_clock> clock_;
};
