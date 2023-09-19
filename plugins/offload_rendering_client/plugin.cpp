#include "illixr/data_format.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"
#include "illixr/vk/vulkan_utils.hpp"

#include <cstdlib>
#include <set>

using namespace ILLIXR;

class offload_rendering_client : public threadloop {
public:
    offload_rendering_client(const std::string& name, phonebook* pb)
        : threadloop{name, pb} {
        std::vector<char *> instance_extensions;
        instance_extensions.push_back(VK_KHR_VIDEO_QUEUE_EXTENSION_NAME);
        instance_extensions.push_back(VK_KHR_VIDEO_DECODE_QUEUE_EXTENSION_NAME);
        // set env
//        setenv("ILLIXR_VULKAN_INSTANCE_EXTENSIONS",  , 1);
    }

    void start() override {
        threadloop::start();
    }

    void stop() override {
        threadloop::stop();
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