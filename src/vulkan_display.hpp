#include <vulkan/vulkan.h>

#define VMA_IMPLEMENTATION

#include "display/glfw_extended.h"
#include "display/headless.hpp"
#include "display/x11_direct.h"
#include "illixr/data_format.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"
#include "illixr/vk/display_provider.hpp"
#include "illixr/vk/vulkan_utils.hpp"

#include <set>
#include <thread>

using namespace ILLIXR;


class display_vk : public vulkan::display_provider {
    std::vector<const char*> required_device_extensions = {
        VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME
    };

public:
    explicit display_vk(const phonebook* const pb)
        : sb{pb->lookup_impl<switchboard>()}, rc {pb->lookup_impl<RelativeClock>()} {

    }

    ~display_vk() override {
        running = false;
        if (main_thread.joinable()) {
            main_thread.join();
        }
        cleanup();
    }

    void start(std::set<const char*> instance_extensions, std::set<const char*> device_extensions) {
        auto manual_device_selection = std::getenv("ILLIXR_VULKAN_SELECT_GPU");
        selected_gpu = manual_device_selection ? std::stoi(manual_device_selection) : -1;

        char* env_var = std::getenv("ILLIXR_DISPLAY_MODE");
        if (!strcmp(env_var, "glfw")) {
            std::cout << "Using GLFW" << std::endl;
            backend_type = display_backend::GLFW;
        } else if (!strcmp(env_var, "headless")) {
            std::cout << "Using headless" << std::endl;
            backend_type = display_backend::HEADLESS;
        } else {
            std::cout << "Using X11 direct mode" << std::endl;
            backend_type = display_backend::X11_DIRECT;
            direct_mode = std::stoi(env_var);
        }

        setup(std::move(instance_extensions), std::move(device_extensions));

        if (backend_type == display_backend::GLFW /* || backend_type == display_backend::X11_DIRECT*/) {
            main_thread = std::thread(&display_vk::main_loop, this);
            while (!ready) {
                // yield
                std::this_thread::yield();
            }
        }
    }

    /**
     * @brief This function sets up the GLFW and Vulkan environments. See display_provider::setup().
     */
    void setup(std::set<const char*> instance_extensions, std::set<const char*> device_extensions) {
        if (backend_type == display_backend::GLFW) {
            backend = std::make_shared<glfw_extended>();
        } else if (backend_type == display_backend::X11_DIRECT) {
            backend = std::make_shared<x11_direct>(rc, sb->get_writer<switchboard::event_wrapper<time_point>>("vsync_estimate"));
        } else {
            backend = std::make_shared<headless>();
        }

        if (backend_type != display_backend::HEADLESS) {
            required_device_extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        }

        create_vk_instance(std::move(instance_extensions));
        if (backend_type == display_backend::GLFW) {
            backend->setup_display(vk_instance, nullptr);
            vk_surface = backend->create_surface();
            select_physical_device();
        } else {
            select_physical_device();
            backend->setup_display(vk_instance, vk_physical_device);
            vk_surface = backend->create_surface();
        }

        auto backend_device_extensions = backend->get_required_device_extensions();
        device_extensions.insert(backend_device_extensions.begin(), backend_device_extensions.end());
        create_logical_device(std::move(device_extensions));

        if (backend_type != display_backend::HEADLESS) {
            create_swapchain();
        }
    }

    /**
     * @brief This function recreates the Vulkan swapchain. See display_provider::recreate_swapchain().
     */
    void recreate_swapchain() override {
        if (backend_type == display_backend::HEADLESS) {
            throw std::runtime_error("Cannot recreate swapchain in headless mode!");
        }

        uint32_t width, height;
        if (direct_mode == -1) {
            auto fb_size = std::dynamic_pointer_cast<glfw_extended>(backend)->get_framebuffer_size();
            width  = std::clamp(fb_size.first, swapchain_extent.width, swapchain_extent.width);
            height = std::clamp(fb_size.second, swapchain_extent.height, swapchain_extent.height);
        } else {
            width  = swapchain_extent.width;
            height = swapchain_extent.height;
        }

        vkDeviceWaitIdle(vk_device);

        destroy_swapchain();

        create_swapchain();
    }

    /**
     * @brief This function polls GLFW events. See display_provider::poll_window_events().
     */
    void poll_window_events() override {
        should_poll = true;
    }

private:

    void create_vk_instance(std::set<const char*> instance_extensions) {
        // Enable validation layers if ILLIXR_VULKAN_VALIDATION_LAYERS is set to 1
        bool enable_validation_layers = std::getenv("ILLIXR_VULKAN_VALIDATION_LAYERS") != nullptr && std::stoi(std::getenv("ILLIXR_VULKAN_VALIDATION_LAYERS"));

        VkApplicationInfo app_info{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app_info.pApplicationName   = "ILLIXR Vulkan Display";
        app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        app_info.pEngineName        = "ILLIXR";
        app_info.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
        app_info.apiVersion         = VK_API_VERSION_1_2;

        auto backend_required_instance_extensions = backend->get_required_instance_extensions();
        std::vector<const char*> backend_required_instance_extensions_vec(backend_required_instance_extensions.begin(), backend_required_instance_extensions.end());
        if (enable_validation_layers) {
            backend_required_instance_extensions_vec.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }
        backend_required_instance_extensions_vec.insert(backend_required_instance_extensions_vec.end(),
                                                        instance_extensions.begin(), instance_extensions.end());

        this->enabled_instance_extensions = backend_required_instance_extensions_vec;

        VkInstanceCreateInfo create_info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        create_info.pApplicationInfo        = &app_info;
        create_info.enabledExtensionCount   = static_cast<uint32_t>(backend_required_instance_extensions_vec.size());
        create_info.ppEnabledExtensionNames = backend_required_instance_extensions_vec.data();

        // print enabled instance extensions
        std::cout << "Enabled instance extensions:" << std::endl;
        for (const auto& extension : backend_required_instance_extensions_vec) {
            std::cout << "\t" << extension << std::endl;
        }

        // enable validation layers
        const std::vector<const char*> validation_layers = {
            "VK_LAYER_KHRONOS_validation"
        };

        if (enable_validation_layers) {
            create_info.enabledLayerCount   = static_cast<uint32_t>(validation_layers.size());
            create_info.ppEnabledLayerNames = validation_layers.data();

            // debug messenger
            VkDebugUtilsMessengerCreateInfoEXT debug_messenger_create_info{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
            debug_messenger_create_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            debug_messenger_create_info.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            debug_messenger_create_info.pfnUserCallback = [](VkDebugUtilsMessageSeverityFlagBitsEXT      messageSeverity,
                                                             VkDebugUtilsMessageTypeFlagsEXT             messageType,
                                                             const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData) -> VkBool32 {
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

        if (vkCreateInstance(&create_info, nullptr, &vk_instance) != VK_SUCCESS) {
            ILLIXR::abort("Failed to create Vulkan instance!");
        }
    }

    bool is_physical_device_suitable(VkPhysicalDevice const& physical_device) {
        VkPhysicalDeviceProperties device_properties;
        vkGetPhysicalDeviceProperties(physical_device, &device_properties);

        VkPhysicalDeviceFeatures device_features;
        vkGetPhysicalDeviceFeatures(physical_device, &device_features);

        // check if the device supports the extensions we need
        std::set<std::string> unmet_extensions(required_device_extensions.begin(), required_device_extensions.end());

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

        if (backend_type == display_backend::GLFW) {
            // check if the device supports the swapchain we need
            auto swapchain_details = vulkan::query_swapchain_details(physical_device, vk_surface);
            if (swapchain_details.formats.empty() || swapchain_details.present_modes.empty()) {
                return false;
            }
        }

        return true;
    }

    void select_physical_device() {
        uint32_t device_count = 0;
        vkEnumeratePhysicalDevices(vk_instance, &device_count, nullptr);

        if (device_count == 0) {
            ILLIXR::abort("No Vulkan devices found!");
        }

        std::vector<VkPhysicalDevice> devices(device_count);
        vkEnumeratePhysicalDevices(vk_instance, &device_count, devices.data());

        std::vector<VkPhysicalDevice> suitable_devices;
        std::vector<VkPhysicalDevice> unsuitable_devices;
        for (const auto& device : devices) {
            if (is_physical_device_suitable(device)) {
                suitable_devices.push_back(device);
            } else {
                unsuitable_devices.push_back(device);
            }
        }

        std::cout << "Found " << device_count << " Vulkan devices" << std::endl;
        int index = 0;
        for (const auto& device : suitable_devices) {
            VkPhysicalDeviceProperties device_properties;
            vkGetPhysicalDeviceProperties(device, &device_properties);
            std::cout << "\t" << "[" << index << "] " << device_properties.deviceName << std::endl;
            index++;
        }
        if (unsuitable_devices.size() > 0) {
            std::cout << "Found " << unsuitable_devices.size() << " unsuitable Vulkan devices" << std::endl;
            for (const auto& device : unsuitable_devices) {
                VkPhysicalDeviceProperties device_properties;
                vkGetPhysicalDeviceProperties(device, &device_properties);
                std::cout << "\t" << device_properties.deviceName << std::endl;
            }
        }

        if (selected_gpu == -1) {
            // select the first suitable device
            vk_physical_device = suitable_devices[0];
        } else {
            // select the device specified by the user
            vk_physical_device = suitable_devices[selected_gpu];
        }

        VkPhysicalDeviceProperties device_properties;
        vkGetPhysicalDeviceProperties(vk_physical_device, &device_properties);
        std::cout << "Selected device: " << device_properties.deviceName << std::endl;
    }

    void create_logical_device(std::set<const char*> device_extensions) {
        vulkan::queue_families indices = vulkan::find_queue_families(vk_physical_device, vk_surface, backend_type == display_backend::HEADLESS);

        std::vector<VkDeviceQueueCreateInfo> queue_create_infos;
        std::set<uint32_t> unique_queue_families = {indices.graphics_family.value()};

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

        VkPhysicalDeviceSynchronization2FeaturesKHR synchronization_2_features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES, nullptr, true};

        VkPhysicalDeviceTimelineSemaphoreFeatures timeline_semaphore_features{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES,
            &synchronization_2_features, // pNext
            VK_TRUE  // timelineSemaphore
        };

        features = VkPhysicalDeviceFeatures2 {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
            &timeline_semaphore_features,
            device_features
        };

        required_device_extensions.insert(required_device_extensions.end(), device_extensions.begin(), device_extensions.end());

        this->enabled_device_extensions = required_device_extensions;

        VkDeviceCreateInfo create_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        create_info.pQueueCreateInfos       = queue_create_infos.data();
        create_info.queueCreateInfoCount    = static_cast<uint32_t>(queue_create_infos.size());
        create_info.enabledExtensionCount   = static_cast<uint32_t>(required_device_extensions.size());
        create_info.ppEnabledExtensionNames = required_device_extensions.data();
        create_info.pNext                   = &features;

        // print enabled device extensions
        std::cout << "Enabled device extensions:" << std::endl;
        for (const auto& extension : required_device_extensions) {
            std::cout << "\t" << extension << std::endl;
        }

        VK_ASSERT_SUCCESS(vkCreateDevice(vk_physical_device, &create_info, nullptr, &vk_device));

        vulkan::queue graphics_queue{};
        vkGetDeviceQueue(vk_device, indices.graphics_family.value(), 0, &graphics_queue.vk_queue);
        graphics_queue.family = indices.graphics_family.value();
        graphics_queue.type   = vulkan::queue::GRAPHICS;
        graphics_queue.mutex = std::make_shared<std::mutex>();
        queues[graphics_queue.type] = graphics_queue;

        if (indices.present_family.has_value()) {
            vulkan::queue present_queue{};
            vkGetDeviceQueue(vk_device, indices.present_family.value(), 0, &present_queue.vk_queue);
            present_queue.family = indices.present_family.value();
            present_queue.type   = vulkan::queue::PRESENT;
            present_queue.mutex = std::make_shared<std::mutex>();
            queues[present_queue.type] = present_queue;
        }

        if (indices.has_compression()) {
            vulkan::queue encode_queue{};
            vkGetDeviceQueue(vk_device, indices.encode_family.value(), 0, &encode_queue.vk_queue);
            encode_queue.family = indices.encode_family.value();
            encode_queue.type   = vulkan::queue::ENCODE;
            encode_queue.mutex = std::make_shared<std::mutex>();
            queues[encode_queue.type] = encode_queue;

            vulkan::queue decode_queue{};
            vkGetDeviceQueue(vk_device, indices.decode_family.value(), 0, &decode_queue.vk_queue);
            decode_queue.family = indices.decode_family.value();
            decode_queue.type   = vulkan::queue::DECODE;
            decode_queue.mutex = std::make_shared<std::mutex>();
            queues[decode_queue.type] = decode_queue;
        }

        if (indices.compute_family.has_value()) {
            vulkan::queue compute_queue{};
            vkGetDeviceQueue(vk_device, indices.compute_family.value(), 0, &compute_queue.vk_queue);
            compute_queue.family = indices.compute_family.value();
            compute_queue.type   = vulkan::queue::COMPUTE;
            compute_queue.mutex = std::make_shared<std::mutex>();
            queues[compute_queue.type] = compute_queue;
        }

        if (indices.dedicated_transfer.has_value()) {
            vulkan::queue transfer_queue{};
            vkGetDeviceQueue(vk_device, indices.dedicated_transfer.value(), 0, &transfer_queue.vk_queue);
            transfer_queue.family = indices.dedicated_transfer.value();
            transfer_queue.type   = vulkan::queue::DEDICATED_TRANSFER;
            transfer_queue.mutex = std::make_shared<std::mutex>();
            queues[transfer_queue.type] = transfer_queue;
        }

        vma_allocator = vulkan::create_vma_allocator(vk_instance, vk_physical_device, vk_device);
    }

    /**
     * @brief Sets up the Vulkan environment.
     *
     * This function initializes the Vulkan instance, selects the physical device, creates the Vulkan device,
     * gets the graphics and present queues, creates the swapchain, and sets up the VMA allocator.
     *
     * @throws runtime_error If any of the Vulkan setup steps fail.
     */
    void create_swapchain() {
        // create surface
        vulkan::swapchain_details swapchain_details = vulkan::query_swapchain_details(vk_physical_device, vk_surface);

        // choose surface format
        for (const auto& available_format : swapchain_details.formats) {
            if (available_format.format == VK_FORMAT_B8G8R8A8_UNORM &&
                available_format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                swapchain_image_format = available_format;
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
            swapchain_extent = swapchain_details.capabilities.currentExtent;
        } else if (std::dynamic_pointer_cast<glfw_extended>(backend) != nullptr) {
            auto fb_size = std::dynamic_pointer_cast<glfw_extended>(backend)->get_framebuffer_size();
            swapchain_extent.width  = std::clamp(fb_size.first, swapchain_details.capabilities.minImageExtent.width, swapchain_details.capabilities.maxImageExtent.width);
            swapchain_extent.height = std::clamp(fb_size.second, swapchain_details.capabilities.minImageExtent.height, swapchain_details.capabilities.maxImageExtent.height);
        }

        uint32_t image_count = std::max(swapchain_details.capabilities.minImageCount, 2u); // double buffering
        if (swapchain_details.capabilities.maxImageCount > 0 && image_count > swapchain_details.capabilities.maxImageCount) {
            image_count = swapchain_details.capabilities.maxImageCount;
        }

        VkSwapchainCreateInfoKHR create_info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        create_info.surface          = vk_surface;
        create_info.minImageCount    = image_count;
        create_info.imageFormat      = swapchain_image_format.format;
        create_info.imageColorSpace  = swapchain_image_format.colorSpace;
        create_info.imageExtent      = swapchain_extent;
        create_info.imageArrayLayers = 1;
        create_info.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        vulkan::queue_families indices = vulkan::find_queue_families(vk_physical_device, vk_surface);
        uint32_t queue_family_indices[] = {indices.graphics_family.value(), indices.present_family.value()};

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
        create_info.presentMode   = swapchain_present_mode;
        create_info.clipped       = VK_TRUE;
        create_info.oldSwapchain  = VK_NULL_HANDLE;

        auto create_shared_swapchains = (PFN_vkCreateSharedSwapchainsKHR)vkGetInstanceProcAddr(vk_instance, "vkCreateSharedSwapchainsKHR");

        if (vkCreateSwapchainKHR(vk_device, &create_info, nullptr, &vk_swapchain) != VK_SUCCESS) {
            ILLIXR::abort("Failed to create Vulkan swapchain!");
        }

        // get swapchain images
        vkGetSwapchainImagesKHR(vk_device, vk_swapchain, &image_count, nullptr);
        swapchain_images.resize(image_count);
        vkGetSwapchainImagesKHR(vk_device, vk_swapchain, &image_count, swapchain_images.data());

        swapchain_image_views.resize(swapchain_images.size());
        for (size_t i = 0; i < swapchain_images.size(); i++) {
            swapchain_image_views[i] = vulkan::create_image_view(vk_device, swapchain_images[i], swapchain_image_format.format, VK_IMAGE_ASPECT_COLOR_BIT);
        }
    }

    void destroy_swapchain() {
        for (auto& image_view : swapchain_image_views) {
            vkDestroyImageView(vk_device, image_view, nullptr);
        }
        vkDestroySwapchainKHR(vk_device, vk_swapchain, nullptr);
    }

    void cleanup() {
        vkDeviceWaitIdle(vk_device);
        destroy_swapchain();

        vkDestroyDevice(vk_device, nullptr);

        vmaDestroyAllocator(vma_allocator);

        if (direct_mode == -1) {
            vkDestroySurfaceKHR(vk_instance, vk_surface, nullptr);
        }

        vkDestroyInstance(vk_instance, nullptr);

        backend->cleanup();
    }

    void main_loop() {
        ready = true;

        while (running) {
            if (direct_mode == -1) {
                if (should_poll.exchange(false)) {
                    auto glfw_backend = std::dynamic_pointer_cast<glfw_extended>(backend);
                    glfw_backend->poll_window_events();
                }
            } else {
                auto x11_backend = std::dynamic_pointer_cast<x11_direct>(backend);
                if (!x11_backend->display_timings_event_registered) {
                    x11_backend->register_display_timings_event(vk_device);
                } else {
                    x11_backend->tick();
                }
            }
        }
    }


private:
    std::thread                 main_thread;
    std::atomic<bool>           ready{false};
    std::atomic<bool>           running{true};

    display_backend::display_backend_type backend_type;
    int direct_mode{-1};
    int selected_gpu{-1};

    std::shared_ptr<display_backend> backend;

    const std::shared_ptr<switchboard> sb;

    std::atomic<bool> should_poll{true};

    friend class display_vk_runner;
    std::shared_ptr<RelativeClock> rc;

};
