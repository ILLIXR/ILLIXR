#include "illixr/data_format.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"
#include "illixr/vk/display_provider.hpp"
#include "illixr/vk/vk_extension_request.h"
#include "illixr/vk/vulkan_utils.hpp"
#include "illixr/vk/render_pass.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_vulkan.h>
}
#include <cstdlib>
#include <set>

using namespace ILLIXR;

void AV_ASSERT_SUCCESS(int ret) {
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
        throw std::runtime_error{std::string{"FFmpeg error: "} + errbuf};
    }
}

class offload_rendering_server
    : public threadloop
    , public vulkan::timewarp
    , public vulkan::vk_extension_request
    , std::enable_shared_from_this<plugin>{
public:
    offload_rendering_server(const std::string& name, phonebook* pb)
        : threadloop{name, pb}
        , log{spdlogger(nullptr)}
        , dp{pb->lookup_impl<vulkan::display_provider>()} {}

    void start() override {
        auto ref              = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_VULKAN);
        auto hwdev_ctx        = reinterpret_cast<AVHWDeviceContext*>(ref->data);
        auto vulkan_hwdev_ctx = reinterpret_cast<AVVulkanDeviceContext*>(hwdev_ctx->hwctx);

        vulkan_hwdev_ctx->inst            = dp->vk_instance;
        vulkan_hwdev_ctx->phys_dev        = dp->vk_physical_device;
        vulkan_hwdev_ctx->act_dev         = dp->vk_device;
        vulkan_hwdev_ctx->device_features = dp->features;
        for (auto& queue : dp->queues) {
            switch (queue.first) {
            case vulkan::vulkan_utils::queue::GRAPHICS:
                vulkan_hwdev_ctx->queue_family_index      = queue.second.family;
                vulkan_hwdev_ctx->nb_graphics_queues      = 1;
                vulkan_hwdev_ctx->queue_family_tx_index   = queue.second.family;
                vulkan_hwdev_ctx->nb_tx_queues            = 1;
                vulkan_hwdev_ctx->queue_family_comp_index = queue.second.family;
                vulkan_hwdev_ctx->nb_comp_queues          = 1;
                break;
            default:
                break;
            }
        }

        // not using Vulkan Video for now
        vulkan_hwdev_ctx->nb_encode_queues          = 0;
        vulkan_hwdev_ctx->nb_decode_queues          = 0;
        vulkan_hwdev_ctx->queue_family_encode_index = -1;
        vulkan_hwdev_ctx->queue_family_decode_index = -1;

        vulkan_hwdev_ctx->alloc         = nullptr;
        vulkan_hwdev_ctx->get_proc_addr = vkGetInstanceProcAddr;

        vulkan_hwdev_ctx->enabled_inst_extensions    = dp->enabled_instance_extensions.data();
        vulkan_hwdev_ctx->nb_enabled_inst_extensions = dp->enabled_instance_extensions.size();
        vulkan_hwdev_ctx->enabled_dev_extensions     = dp->enabled_device_extensions.data();
        vulkan_hwdev_ctx->nb_enabled_dev_extensions  = dp->enabled_device_extensions.size();

        AV_ASSERT_SUCCESS(av_hwdevice_ctx_init(ref));
        log->info("FFmpeg Vulkan hwdevice context initialized");
    }

    std::vector<const char*> get_required_instance_extensions() override {
        return {};
    }

    std::vector<const char*> get_required_devices_extensions() override {
        std::vector<const char*> device_extensions;
        device_extensions.push_back(VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);
        device_extensions.push_back(VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME);
        return device_extensions;
    }

    virtual void setup(VkRenderPass render_pass, uint32_t subpass,
                       std::shared_ptr<vulkan::buffer_pool<pose_type>> buffer_pool,
                       bool input_texture_vulkan_coordinates) override {

    }

    virtual void record_command_buffer(VkCommandBuffer commandBuffer, int buffer_ind, int eye) override {

    }

    void update_uniforms(const pose_type& render_pose) override {

    }

    void destroy() override {

    }

    bool is_external() override {
        return true;
    }

protected:
    skip_option _p_should_skip() override {
        return threadloop::_p_should_skip();
    }

    void _p_thread_setup() override {
        AVFormatContext* pFormatCtx = nullptr;
    }

    void _p_one_iteration() override { }

private:
    std::array<std::vector<AVVkFrame>, 2> buffer_pool;
    std::shared_ptr<spdlog::logger>           log;
    std::shared_ptr<vulkan::display_provider> dp;

    void create_avvkframe(VkImageView image_view) {

    }
};

class offload_rendering_server_loader : public plugin {
public:
    offload_rendering_server_loader(const std::string& name, phonebook* pb)
        : plugin(name, pb), offload_rendering_server_plugin{std::make_shared<offload_rendering_server>(name, pb)} {
        pb->register_impl<vulkan::timewarp>(offload_rendering_server_plugin);
    }

    void start() override {
        offload_rendering_server_plugin->start();
    }

    void stop() override {
        offload_rendering_server_plugin->stop();
    }

private:
    std::shared_ptr<offload_rendering_server> offload_rendering_server_plugin;
};

PLUGIN_MAIN(offload_rendering_server)