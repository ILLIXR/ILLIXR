#include "illixr/data_format.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/serializable_data.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"
#include "illixr/vk/display_provider.hpp"
#include "illixr/vk/ffmpeg_utils.hpp"
#include "illixr/vk/render_pass.hpp"
#include "illixr/vk/vk_extension_request.h"
#include "illixr/vk/vulkan_utils.hpp"

#include <cstdlib>
#include <set>

#define OFFLOAD_RENDERING_FFMPEG_DECODER_NAME "h264"

using namespace ILLIXR;
using namespace ILLIXR::vulkan::ffmpeg_utils;

class offload_rendering_client
    : public threadloop
    , public vulkan::app
    , std::enable_shared_from_this<plugin> {
public:
    offload_rendering_client(const std::string& name, phonebook* pb)
        : threadloop{name, pb}
        , sb{pb->lookup_impl<switchboard>()}
        , log{spdlogger(nullptr)}
        , dp{pb->lookup_impl<vulkan::display_provider>()}
        , frames_reader{sb->get_buffered_reader<compressed_frame>("compressed_frames")}{
        display_provider_ffmpeg = dp;
    }

    void start() override {
        ffmpeg_init_device();
        ffmpeg_init_cuda_device();
        threadloop::start();
    }

    virtual void setup(VkRenderPass render_pass, uint32_t subpass, std::shared_ptr<vulkan::buffer_pool<pose_type>> buffer_pool) override {
        this->buffer_pool = buffer_pool;
        ffmpeg_init_frame_ctx();
        ffmpeg_init_cuda_frame_ctx();
        ffmpeg_init_buffer_pool();
        ffmpeg_init_decoder();
        ready = true;
    }

    bool is_external() override {
        return true;
    }

    void destroy() override {
        for (auto& frame : avvkframes) {
            for (auto& eye : frame) {
                av_frame_free(&eye.frame);
            }
        }
        av_buffer_unref(&frame_ctx);
        av_buffer_unref(&device_ctx);
    }

protected:
    void _p_thread_setup() override {
        // rename thread
        pthread_setname_np(pthread_self(), "offload_rendering_client");
    }

    skip_option _p_should_skip() override {
        return threadloop::_p_should_skip();
    }

    void copy_image_to_cpu_and_save_file(AVFrame* frame) {
        auto cpu_av_frame    = av_frame_alloc();
        cpu_av_frame->format = AV_PIX_FMT_RGBA;
        auto ret             = av_hwframe_transfer_data(cpu_av_frame, frame, 0);
        AV_ASSERT_SUCCESS(ret);

        // save cpu_av_frame as png
        auto png_codec           = avcodec_find_encoder(AV_CODEC_ID_PNG);
        auto png_codec_ctx       = avcodec_alloc_context3(png_codec);
        png_codec_ctx->pix_fmt   = AV_PIX_FMT_RGBA;
        png_codec_ctx->width     = cpu_av_frame->width;
        png_codec_ctx->height    = cpu_av_frame->height;
        png_codec_ctx->time_base = {1, 60};
        png_codec_ctx->framerate = {60, 1};

        ret = avcodec_open2(png_codec_ctx, png_codec, nullptr);
        AV_ASSERT_SUCCESS(ret);
        AVPacket* png_packet = av_packet_alloc();
        ret                  = avcodec_send_frame(png_codec_ctx, cpu_av_frame);
        AV_ASSERT_SUCCESS(ret);
        ret = avcodec_receive_packet(png_codec_ctx, png_packet);
        AV_ASSERT_SUCCESS(ret);

        std::string filename = "frame_" + std::to_string(frame_count) + ".png";
        FILE*       f        = fopen(filename.c_str(), "wb");
        fwrite(png_packet->data, 1, png_packet->size, f);
        fclose(f);

        av_packet_free(&png_packet);
        av_frame_free(&cpu_av_frame);
        avcodec_free_context(&png_codec_ctx);
    }

    void _p_one_iteration() override {
        if (!ready) {
            return;
        }
        auto pose = network_receive();

        // receive packets
        for (auto eye = 0; eye < 2; eye++) {
            auto ret = avcodec_send_packet(codec_ctx, decode_src_packets[eye]);
            if (ret == AVERROR(EAGAIN)) {
                throw std::runtime_error{"FFmpeg encoder returned EAGAIN. Internal buffer full? Try using a higher-end GPU."};
            }
            AV_ASSERT_SUCCESS(ret);
//            ret = avcodec_receive_frame(codec_ctx, decode_out_frames[eye]);
//            AV_ASSERT_SUCCESS(ret);
//            copy_image_to_cpu_and_save_file(decode_out_frames[eye]);
//            decode_out_frames[eye]->pts = frame_count++;
        }

        auto ind = buffer_pool->src_acquire_image();

        for (auto eye = 0; eye < 2; eye++) {
            auto ret = avcodec_receive_frame(codec_ctx, decode_out_frames[eye]);
            assert(decode_out_frames[eye]->format == AV_PIX_FMT_CUDA);
            AV_ASSERT_SUCCESS(ret);
//            copy_image_to_cpu_and_save_file(decode_out_frames[eye]);
//            ret = av_hwframe_transfer_data(avvkframes[ind][eye].frame, decode_out_frames[eye], 0);
//            AV_ASSERT_SUCCESS(ret);
//            vulkan::wait_timeline_semaphore(dp->vk_device, avvkframes[ind][eye].vk_frame->sem[0],
//                                                          avvkframes[ind][eye].vk_frame->sem_value[0]);
            decode_out_frames[eye]->pts = frame_count++;
        }
        buffer_pool->src_release_image(ind, std::move(pose));
        // log->info("Sending frame {}", frame_count);
    }

private:
    std::shared_ptr<switchboard> sb;
    std::shared_ptr<spdlog::logger>                 log;
    std::shared_ptr<vulkan::display_provider>       dp;
    switchboard::buffered_reader<compressed_frame> frames_reader;
    std::atomic<bool>                               ready = false;

    std::shared_ptr<vulkan::buffer_pool<pose_type>> buffer_pool;
    std::vector<std::array<ffmpeg_vk_frame, 2>>     avvkframes;
    AVBufferRef*    device_ctx;
    AVBufferRef*    cuda_device_ctx;
    AVBufferRef*    frame_ctx;
    AVBufferRef*    cuda_frame_ctx;

    AVCodecContext* codec_ctx;
    std::array<AVPacket*, 2>  decode_src_packets;
    std::array<AVFrame*, 2>   decode_out_frames;

    uint64_t                 frame_count = 0;

    pose_type network_receive() {
        if (decode_src_packets[0] != nullptr) {
            av_packet_free_side_data(decode_src_packets[0]);
            av_packet_free_side_data(decode_src_packets[1]);
            av_packet_free(&decode_src_packets[0]);
            av_packet_free(&decode_src_packets[1]);
        }
        auto frame = frames_reader.dequeue();
        decode_src_packets[0] = frame->left;
        decode_src_packets[1] = frame->right;
        log->info("Received frame {}", frame_count);
        return {frame->pose};
    }

    void ffmpeg_init_device() {
        this->device_ctx      = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_VULKAN);
        auto hwdev_ctx        = reinterpret_cast<AVHWDeviceContext*>(device_ctx->data);
        auto vulkan_hwdev_ctx = reinterpret_cast<AVVulkanDeviceContext*>(hwdev_ctx->hwctx);

        vulkan_hwdev_ctx->inst            = dp->vk_instance;
        vulkan_hwdev_ctx->phys_dev        = dp->vk_physical_device;
        vulkan_hwdev_ctx->act_dev         = dp->vk_device;
        vulkan_hwdev_ctx->device_features = dp->features;
        for (auto& queue : dp->queues) {
            switch (queue.first) {
            case vulkan::queue::GRAPHICS:
                vulkan_hwdev_ctx->queue_family_index    = queue.second.family;
                vulkan_hwdev_ctx->nb_graphics_queues    = 1;
                vulkan_hwdev_ctx->queue_family_tx_index = queue.second.family;
                vulkan_hwdev_ctx->nb_tx_queues          = 1;
                break;
            case vulkan::queue::COMPUTE:
                vulkan_hwdev_ctx->queue_family_comp_index = queue.second.family;
                vulkan_hwdev_ctx->nb_comp_queues          = 1;
            default:
                break;
            }
        }

        if (dp->queues.find(vulkan::queue::DEDICATED_TRANSFER) != dp->queues.end()) {
            vulkan_hwdev_ctx->queue_family_tx_index = dp->queues[vulkan::queue::DEDICATED_TRANSFER].family;
            vulkan_hwdev_ctx->nb_tx_queues          = 1;
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

        vulkan_hwdev_ctx->lock_queue = &ffmpeg_lock_queue;
        vulkan_hwdev_ctx->unlock_queue = &ffmpeg_unlock_queue;

        AV_ASSERT_SUCCESS(av_hwdevice_ctx_init(device_ctx));
        log->info("FFmpeg Vulkan hwdevice context initialized");
    }

    void ffmpeg_init_cuda_device() {
        auto ret = av_hwdevice_ctx_create(&cuda_device_ctx, AV_HWDEVICE_TYPE_CUDA, nullptr, nullptr, 0);
        AV_ASSERT_SUCCESS(ret);
        if (cuda_device_ctx == nullptr) {
            throw std::runtime_error{"Failed to create FFmpeg CUDA hwdevice context"};
        }

        log->info("FFmpeg CUDA hwdevice context initialized");
    }

    void ffmpeg_init_frame_ctx() {
        assert(this->buffer_pool != nullptr);
        this->frame_ctx = av_hwframe_ctx_alloc(device_ctx);
        if (!frame_ctx) {
            throw std::runtime_error{"Failed to create FFmpeg Vulkan hwframe context"};
        }

        auto hwframe_ctx    = reinterpret_cast<AVHWFramesContext*>(frame_ctx->data);
        hwframe_ctx->format = AV_PIX_FMT_VULKAN;
        auto pix_format = vulkan::ffmpeg_utils::get_pix_format_from_vk_format(buffer_pool->image_pool[0][0].image_info.format);
        if (!pix_format) {
            throw std::runtime_error{"Unsupported Vulkan image format when creating FFmpeg Vulkan hwframe context"};
        }
        assert(pix_format == AV_PIX_FMT_BGRA);
        hwframe_ctx->sw_format         = AV_PIX_FMT_BGRA;
        hwframe_ctx->width             = buffer_pool->image_pool[0][0].image_info.extent.width;
        hwframe_ctx->height            = buffer_pool->image_pool[0][0].image_info.extent.height;
        hwframe_ctx->initial_pool_size = 0;
        auto ret                       = av_hwframe_ctx_init(frame_ctx);
        AV_ASSERT_SUCCESS(ret);
    }

    void ffmpeg_init_cuda_frame_ctx() {
        assert(this->buffer_pool != nullptr);
        auto cuda_frame_ref = av_hwframe_ctx_alloc(cuda_device_ctx);
        if (!cuda_frame_ref) {
            throw std::runtime_error{"Failed to create FFmpeg CUDA hwframe context"};
        }
        auto cuda_hwframe_ctx       = reinterpret_cast<AVHWFramesContext*>(cuda_frame_ref->data);
        cuda_hwframe_ctx->format    = AV_PIX_FMT_CUDA;
        cuda_hwframe_ctx->sw_format = AV_PIX_FMT_NV12;
        cuda_hwframe_ctx->width     = buffer_pool->image_pool[0][0].image_info.extent.width;
        cuda_hwframe_ctx->height    = buffer_pool->image_pool[0][0].image_info.extent.height;
        // cuda_hwframe_ctx->initial_pool_size = 0;
        auto ret = av_hwframe_ctx_init(cuda_frame_ref);
        AV_ASSERT_SUCCESS(ret);
        this->cuda_frame_ctx = cuda_frame_ref;
    }

    void ffmpeg_init_buffer_pool() {
        assert(this->buffer_pool != nullptr);
        avvkframes.resize(buffer_pool->image_pool.size());
        for (size_t i = 0; i < buffer_pool->image_pool.size(); i++) {
            for (size_t eye = 0; eye < 2; eye++) {
                // Create AVVkFrame
                auto vk_frame = av_vk_frame_alloc();
                if (!vk_frame) {
                    throw std::runtime_error{"Failed to allocate FFmpeg Vulkan frame"};
                }
                // The image index is just 0 here for AVVKFrame since we're not using multi-plane
                vk_frame->img[0]          = buffer_pool->image_pool[i][eye].image;
                vk_frame->tiling          = buffer_pool->image_pool[i][eye].image_info.tiling;
                vk_frame->mem[0]          = buffer_pool->image_pool[i][eye].allocation_info.deviceMemory;
                vk_frame->size[0]         = buffer_pool->image_pool[i][eye].allocation_info.size;
                vk_frame->offset[0]       = buffer_pool->image_pool[i][eye].allocation_info.offset;
                vk_frame->queue_family[0] = dp->queues[vulkan::queue::GRAPHICS].family;

                VkExportSemaphoreCreateInfo export_semaphore_create_info{
                    VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO, nullptr, VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT};
                vk_frame->sem[0] =
                    vulkan::create_timeline_semaphore(dp->vk_device, 0, &export_semaphore_create_info);
                vk_frame->sem_value[0] = 0;
                vk_frame->layout[0]    = VK_IMAGE_LAYOUT_UNDEFINED;

                avvkframes[i][eye].vk_frame = vk_frame;

                // Create AVFrame
                auto av_frame = av_frame_alloc();
                if (!av_frame) {
                    throw std::runtime_error{"Failed to allocate FFmpeg frame"};
                }
                av_frame->format        = AV_PIX_FMT_VULKAN;
                av_frame->width         = buffer_pool->image_pool[i][eye].image_info.extent.width;
                av_frame->height        = buffer_pool->image_pool[i][eye].image_info.extent.height;
                av_frame->hw_frames_ctx = av_buffer_ref(frame_ctx);
                av_frame->data[0]       = reinterpret_cast<uint8_t*>(vk_frame);
                av_frame->buf[0]        = av_buffer_create(
                    av_frame->data[0], 0, [](void*, uint8_t*) {}, nullptr, 0);
                av_frame->pts            = 0;
                avvkframes[i][eye].frame = av_frame;
            }
        }

        for (size_t eye = 0; eye < 2; eye++) {
            decode_out_frames[eye]                = av_frame_alloc();
            decode_out_frames[eye]->format        = AV_PIX_FMT_CUDA;
            decode_out_frames[eye]->hw_frames_ctx = av_buffer_ref(cuda_frame_ctx);
            decode_out_frames[eye]->width         = buffer_pool->image_pool[0][0].image_info.extent.width;
            decode_out_frames[eye]->height        = buffer_pool->image_pool[0][0].image_info.extent.height;
            // encode_src_frames[eye]->buf[0] = av_buffer_alloc(1);
            auto ret = av_hwframe_get_buffer(cuda_frame_ctx, decode_out_frames[eye], 0);
            AV_ASSERT_SUCCESS(ret);
            // std::cout << encode_src_frames[eye]->data[0] << std::endl;
            // auto ret = av_frame_get_buffer(encode_src_frames[eye], 0);
            // AV_ASSERT_SUCCESS(ret);
        }
    }

    void ffmpeg_init_decoder() {
        auto decoder = avcodec_find_decoder_by_name(OFFLOAD_RENDERING_FFMPEG_DECODER_NAME);
        if (!decoder) {
            throw std::runtime_error{"Failed to find FFmpeg decoder"};
        }
        this->codec_ctx = avcodec_alloc_context3(decoder);
        if (!codec_ctx) {
            throw std::runtime_error{"Failed to allocate FFmpeg decoder context"};
        }

        codec_ctx->thread_count = 0; // auto
        codec_ctx->thread_type  = FF_THREAD_SLICE;

        codec_ctx->pix_fmt       = AV_PIX_FMT_CUDA; // alpha channel will be useful later for compositing
        codec_ctx->sw_pix_fmt    = AV_PIX_FMT_NV12;
        codec_ctx->hw_device_ctx = av_buffer_ref(cuda_device_ctx);
        codec_ctx->hw_frames_ctx = av_buffer_ref(cuda_frame_ctx);
        codec_ctx->width         = buffer_pool->image_pool[0][0].image_info.extent.width;
        codec_ctx->height        = buffer_pool->image_pool[0][0].image_info.extent.height;
        codec_ctx->framerate     = {0, 1};
        codec_ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
//        codec_ctx->flags2 |= AV_CODEC_FLAG2_CHUNKS;

        // Set zero latency
        av_opt_set_int(codec_ctx->priv_data, "zerolatency", 1, 0);
        av_opt_set_int(codec_ctx->priv_data, "delay", 0, 0);

        av_opt_set(codec_ctx->priv_data, "hwaccel", "cuda", 0);

        // Set decoder profile
        // av_opt_set(codec_ctx->priv_data, "profile", "high", 0);

        // Set decoder preset
        // av_opt_set(codec_ctx->priv_data, "preset", "fast", 0);

        auto ret = avcodec_open2(codec_ctx, decoder, nullptr);
        AV_ASSERT_SUCCESS(ret);
    }

};

class offload_rendering_client_loader
    : public plugin
    , public vulkan::vk_extension_request {
public:
    offload_rendering_client_loader(const std::string& name, phonebook* pb)
        : plugin(name, pb)
        , offload_rendering_client_plugin{std::make_shared<offload_rendering_client>(name, pb)} {
        pb->register_impl<vulkan::app>(offload_rendering_client_plugin);
        std::cout << "Registered vulkan::timewarp" << std::endl;
    }

    std::vector<const char*> get_required_instance_extensions() override {
        return {VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME, VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME,
                VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME};
    }

    std::vector<const char*> get_required_devices_extensions() override {
        std::vector<const char*> device_extensions;
        device_extensions.push_back(VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME);
        device_extensions.push_back(VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME);
        device_extensions.push_back(VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);
        device_extensions.push_back(VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME);
        device_extensions.push_back(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
        return device_extensions;
    }

    void start() override {
        offload_rendering_client_plugin->start();
    }

    void stop() override {
        offload_rendering_client_plugin->stop();
    }

private:
    std::shared_ptr<offload_rendering_client> offload_rendering_client_plugin;
};

PLUGIN_MAIN(offload_rendering_client_loader)