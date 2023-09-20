#define VMA_IMPLEMENTATION
#include "illixr/data_format.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"
#include "illixr/vk_util/display_sink.hpp"

using namespace ILLIXR;

class display_vk : public display_sink {
public:
    explicit display_vk(const phonebook* const pb)
        : sb{pb->lookup_impl<switchboard>()} { }

    /**
     * @brief This function sets up the GLFW and Vulkan environments. See display_sink::setup().
     */
    void setup() {
        setup_glfw();
        setup_vk();
    }

    /**
     * @brief This function recreates the Vulkan swapchain. See display_sink::recreate_swapchain().
     */
    void recreate_swapchain() override {
        vkb::SwapchainBuilder swapchain_builder{vkb_device};
        auto                  swapchain_ret = swapchain_builder.set_old_swapchain(vk_swapchain)
                                 .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
                                 .set_desired_extent(display_params::width_pixels, display_params::height_pixels)
                                 .build();
        if (!swapchain_ret) {
            ILLIXR::abort("Failed to create Vulkan swapchain. Error: " + swapchain_ret.error().message());
        }
        vkb_swapchain          = swapchain_ret.value();
        vk_swapchain           = vkb_swapchain.swapchain;
        swapchain_images       = vkb_swapchain.get_images().value();
        swapchain_image_views  = vkb_swapchain.get_image_views().value();
        swapchain_image_format = vkb_swapchain.image_format;
        swapchain_extent       = vkb_swapchain.extent;
    }

    /**
     * @brief This function polls GLFW events. See display_sink::poll_window_events().
     */
    void poll_window_events() override {
        should_poll = true;
    }

private:
    /**
     * @brief Sets up the GLFW environment.
     *
     * This function initializes the GLFW library, sets the window hints for the client API and resizability,
     * and creates a GLFW window with the specified width and height.
     *
     * @throws runtime_error If GLFW initialization fails.
     */
    void setup_glfw() {
        if (!glfwInit()) {
            ILLIXR::abort("Failed to initalize glfw");
        }

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);

        window = glfwCreateWindow(display_params::width_pixels, display_params::height_pixels,
                                  "ILLIXR Eyebuffer Window (Vulkan)", nullptr, nullptr);
    }

    /**
     * @brief Sets up the Vulkan environment.
     *
     * This function initializes the Vulkan instance, selects the physical device, creates the Vulkan device,
     * gets the graphics and present queues, creates the swapchain, and sets up the VMA allocator.
     *
     * @throws runtime_error If any of the Vulkan setup steps fail.
     */
    void setup_vk() {
        vkb::InstanceBuilder builder;
        auto                 instance_ret =
            builder.set_app_name("ILLIXR Vulkan Display")
                .require_api_version(1, 2)
                .request_validation_layers()
                .enable_validation_layers()
                .set_debug_callback([](VkDebugUtilsMessageSeverityFlagBitsEXT      messageSeverity,
                                       VkDebugUtilsMessageTypeFlagsEXT             messageType,
                                       const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData) -> VkBool32 {
                    auto severity = vkb::to_string_message_severity(messageSeverity);
                    auto type     = vkb::to_string_message_type(messageType);
                    spdlog::get("illixr")->debug("[display_vk] [{}: {}] {}", severity, type, pCallbackData->pMessage);
                    return VK_FALSE;
                })
                .build();
        if (!instance_ret) {
            ILLIXR::abort("Failed to create Vulkan instance. Error: " + instance_ret.error().message());
        }
        vkb_instance = instance_ret.value();
        vk_instance  = vkb_instance.instance;

        vkb::PhysicalDeviceSelector selector{vkb_instance};
        if (glfwCreateWindowSurface(vkb_instance.instance, (GLFWwindow*) window, nullptr, &vk_surface) != VK_SUCCESS) {
            throw std::runtime_error("failed to create window surface!");
        }

        auto physical_device_ret = selector.set_surface(vk_surface)
                                       .set_minimum_version(1, 2)
                                       .prefer_gpu_device_type(vkb::PreferredDeviceType::discrete)
                                       // .add_required_extension(VK_EXT_DISPLAY_CONTROL_EXTENSION_NAME)
                                       .select();

        if (!physical_device_ret) {
            ILLIXR::abort("Failed to select Vulkan Physical Device. Error: " + physical_device_ret.error().message());
        }
        physical_device    = physical_device_ret.value();
        vk_physical_device = physical_device.physical_device;

        vkb::DeviceBuilder device_builder{physical_device};

        // enable timeline semaphore
        VkPhysicalDeviceTimelineSemaphoreFeatures timeline_semaphore_features{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES,
            nullptr, // pNext
            VK_TRUE  // timelineSemaphore
        };

        // enable anisotropic filtering
        auto device_ret = device_builder.add_pNext(&timeline_semaphore_features).build();
        if (!device_ret) {
            ILLIXR::abort("Failed to create Vulkan device. Error: " + device_ret.error().message());
        }
        vkb_device = device_ret.value();
        vk_device  = vkb_device.device;

        auto graphics_queue_ret = vkb_device.get_queue(vkb::QueueType::graphics);
        if (!graphics_queue_ret) {
            ILLIXR::abort("Failed to get Vulkan graphics queue. Error: " + graphics_queue_ret.error().message());
        }
        graphics_queue        = graphics_queue_ret.value();
        graphics_queue_family = vkb_device.get_queue_index(vkb::QueueType::graphics).value();

        auto present_queue_ret = vkb_device.get_queue(vkb::QueueType::present);
        if (!present_queue_ret) {
            ILLIXR::abort("Failed to get Vulkan present queue. Error: " + present_queue_ret.error().message());
        }
        present_queue        = present_queue_ret.value();
        present_queue_family = vkb_device.get_queue_index(vkb::QueueType::present).value();

        vkb::SwapchainBuilder swapchain_builder{vkb_device};
        auto swapchain_ret = swapchain_builder.set_desired_format({VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
                                 .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
                                 .set_desired_extent(display_params::width_pixels, display_params::height_pixels)
                                 .build();
        if (!swapchain_ret) {
            ILLIXR::abort("Failed to create Vulkan swapchain. Error: " + swapchain_ret.error().message());
        }
        vkb_swapchain = swapchain_ret.value();
        vk_swapchain  = vkb_swapchain.swapchain;

        swapchain_images       = vkb_swapchain.get_images().value();
        swapchain_image_views  = vkb_swapchain.get_image_views().value();
        swapchain_image_format = vkb_swapchain.image_format;
        swapchain_extent       = vkb_swapchain.extent;

#ifndef NDEBUG
        spdlog::get("illixr")->debug("[display_vk] present_mode: {}", vkb_swapchain.present_mode);
        spdlog::get("illixr")->debug("[display_vk] swapchain_extent: {} {}", swapchain_extent.width, swapchain_extent.height);
#endif
        vma_allocator = vulkan_utils::create_vma_allocator(vk_instance, vk_physical_device, vk_device);
    }

    const std::shared_ptr<switchboard> sb;
    vkb::Instance                      vkb_instance;
    vkb::PhysicalDevice                physical_device;
    vkb::Device                        vkb_device;
    vkb::Swapchain                     vkb_swapchain;

    std::atomic<bool> should_poll{true};

    friend class display_vk_plugin;
};

class display_vk_plugin : public plugin {
public:
    display_vk_plugin(const std::string& name, phonebook* pb)
        : plugin{name, pb}
        , _dvk{std::make_shared<display_vk>(pb)}
        , _pb{pb} {
        _pb->register_impl<display_sink>(std::static_pointer_cast<display_sink>(_dvk));
    }

    void start() override {
        main_thread = std::thread(&display_vk_plugin::main_loop, this);
        while (!ready) {
            // yield
            std::this_thread::yield();
        }
    }

    void stop() override {
        running = false;
    }

private:
    std::thread                 main_thread;
    std::atomic<bool>           ready{false};
    std::shared_ptr<display_vk> _dvk;
    std::atomic<bool>           running{true};
    phonebook*                  _pb;

    void main_loop() {
        _dvk->setup();

        ready = true;

        while (running) {
            if (_dvk->should_poll.exchange(false)) {
                glfwPollEvents();
            }
        }
    }
};

PLUGIN_MAIN(display_vk_plugin)