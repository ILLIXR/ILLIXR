#include "illixr/data_format.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"
#include "illixr/vk/vulkan_utils.hpp"
#include "illixr/vk/vk_extension_request.h"

#include <cstdlib>
#include <set>

using namespace ILLIXR;

class offload_rendering_client : public threadloop, public vulkan::vk_extension_request {
public:
    offload_rendering_client(const std::string& name, phonebook* pb)
        : threadloop{name, pb} {

    }

    void start() override {
        threadloop::start();
    }

    void stop() override {
        threadloop::stop();
    }

    std::vector<const char*> get_required_instance_extensions() override {
        return {};
    }

    std::vector<const char*> get_required_devices_extensions() override {
        std::vector<const char*> device_extensions;
        device_extensions.push_back(VK_KHR_VIDEO_QUEUE_EXTENSION_NAME);
        device_extensions.push_back(VK_KHR_VIDEO_DECODE_QUEUE_EXTENSION_NAME);
        device_extensions.push_back(VK_KHR_VIDEO_DECODE_H264_EXTENSION_NAME);
        return device_extensions;
    }

protected:
    skip_option _p_should_skip() override {
        return threadloop::_p_should_skip();
    }

    void _p_thread_setup() override {
        threadloop::_p_thread_setup();
    }

    void _p_one_iteration() override { }

private:

};

PLUGIN_MAIN(offload_rendering_client)