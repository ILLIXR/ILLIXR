#include "illixr/data_format.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/pose_prediction.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"
#include "illixr/vk/display_provider.hpp"
#include "illixr/vk/ffmpeg_utils.hpp"
#include "illixr/serializable_data.hpp"
#include "illixr/vk/render_pass.hpp"
#include "illixr/vk/vk_extension_request.h"
#include "illixr/vk/vulkan_utils.hpp"

#include <set>

using namespace ILLIXR;
using namespace ILLIXR::vulkan::ffmpeg_utils;

class offload_rendering_server
    : public threadloop
    , public vulkan::timewarp
    , public pose_prediction
    , std::enable_shared_from_this<plugin> {
public:
    offload_rendering_server(const std::string& name, phonebook* pb)
        : threadloop{name, pb}
        , log{spdlogger("debug")}
        , sb{pb->lookup_impl<switchboard>()}
        , frames_topic{std::move(sb->get_network_writer<compressed_frame>("compressed_frames", {}))}
        , render_pose{sb->get_reader<fast_pose_type>("render_pose")} {
        // Only encode and pass depth if requested - otherwise skip it.
        pass_depth = std::getenv("ILLIXR_USE_DEPTH_IMAGES") != nullptr && std::stoi(std::getenv("ILLIXR_USE_DEPTH_IMAGES"));
        nalu_only = std::getenv("ILLIXR_OFFLOAD_RENDERING_NALU_ONLY") != nullptr && std::stoi(std::getenv("ILLIXR_OFFLOAD_RENDERING_NALU_ONLY"));
        if (pass_depth) {
            log->debug("Encoding depth images for the client");
        } else {
            log->debug("Not encoding depth images for the client");
        }

        if (nalu_only) {
            log->info("Only sending NALUs to the client");
        }
    }

    void start() override {
        threadloop::start();
    }

    void _p_thread_setup() override {
        while (dp == nullptr) {
            try {
                dp = pb->lookup_impl<vulkan::display_provider>();
                display_provider_ffmpeg = dp;
            } catch (const std::exception& e) {
                log->debug("Display provider not ready yet");
                // sleep for 1 second
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
        log->info("Obtained display provider");

        auto bitrate_env = std::getenv("ILLIXR_OFFLOAD_RENDERING_BITRATE");
        if (bitrate_env == nullptr) {
            bitrate = OFFLOAD_RENDERING_BITRATE;
        }
        bitrate = std::stol(bitrate_env);
        if (bitrate <= 0) {
            throw std::runtime_error{"Invalid bitrate value"};
        }
        log->info("Using bitrate: {}", bitrate);

        auto framerate_env = std::getenv("ILLIXR_OFFLOAD_RENDERING_FRAMERATE");
        if (framerate_env == nullptr) {
            framerate = 144;
        }
        framerate = std::stoi(framerate_env);
        if (framerate <= 0) {
            throw std::runtime_error{"Invalid framerate value"};
        }
        log->info("Using framerate: {}", framerate);

        ffmpeg_init_device();
        ffmpeg_init_cuda_device();
        ready = true;
    };

public:
    void setup(VkRenderPass render_pass, uint32_t subpass, std::shared_ptr<vulkan::buffer_pool<fast_pose_type>> _buffer_pool,
               bool input_texture_vulkan_coordinates) override {
        while (!ready) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        this->buffer_pool = _buffer_pool;
        ffmpeg_init_frame_ctx();
        ffmpeg_init_cuda_frame_ctx();
        ffmpeg_init_buffer_pool();
        ffmpeg_init_encoder();
        for (auto eye = 0; eye < 2; eye++) {
            encode_out_color_packets[eye] = av_packet_alloc();
            if (pass_depth)
                encode_out_depth_packets[eye] = av_packet_alloc();
        }
    }

    bool is_external() override {
        return true;
    }

    void destroy() override {
        for (auto& frame : avvk_color_frames) {
            for (auto& eye : frame) {
                av_frame_free(&eye.frame);
            }
        }
        if (pass_depth) {
            for (auto& frame : avvk_depth_frames) {
                for (auto& eye : frame) {
                    av_frame_free(&eye.frame);
                }
            }
        }
        av_buffer_unref(&frame_ctx);
        av_buffer_unref(&device_ctx);
    }

    fast_pose_type get_fast_pose() const override {
        auto pose = render_pose.get_ro_nullable();
//        auto now = std::chrono::high_resolution_clock::now();
//        auto diff = now - pose->predict_computed_time._m_time_since_epoch;
//        log->info("diff (ms): {}", std::chrono::duration_cast<std::chrono::milliseconds>(diff.time_since_epoch()).count());
        if (pose == nullptr) {
            return {};
        } else {
            return *pose;
        }
    }

    pose_type get_true_pose() const override {
        return get_fast_pose().pose;
    }

    fast_pose_type get_fast_pose(time_point future_time) const override {
        return get_fast_pose();
    }

    bool fast_pose_reliable() const override {
        return render_pose.get_ro_nullable() != nullptr;
    }

    bool true_pose_reliable() const override {
        return false;
    }

    void set_offset(const Eigen::Quaternionf& orientation) override { }

    Eigen::Quaternionf get_offset() override {
        return Eigen::Quaternionf();
    }

    pose_type correct_pose(const pose_type& pose) const override {
        return pose_type();
    }

    void record_command_buffer(VkCommandBuffer commandBuffer, VkFramebuffer framebuffer, int buffer_ind, bool left) override {}
    void update_uniforms(const pose_type& render_pose) override {}

protected:
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

        std::string filename = "frame_encode_" + std::to_string(frame_count) + ".png";
        FILE*       f        = fopen(filename.c_str(), "wb");
        fwrite(png_packet->data, 1, png_packet->size, f);
        fclose(f);

        av_packet_free(&png_packet);
        av_frame_free(&cpu_av_frame);
        avcodec_free_context(&png_codec_ctx);
    }

    void _p_one_iteration() override {
        if (buffer_pool == nullptr || buffer_pool->latest_decoded_image == -1) {
//            log->info("no decoded image, returning");
            return;
        }
        auto acquire_image_start_time = std::chrono::high_resolution_clock::now();
        std::pair<ILLIXR::vulkan::image_index_t, fast_pose_type> res  = buffer_pool->post_processing_acquire_image(last_frame_ind);
        auto acquire_image_end_time = std::chrono::high_resolution_clock::now();
        auto                                                ind  = res.first;
        auto                                                pose = res.second;
        // get timestamp
        auto copy_start_time = std::chrono::high_resolution_clock::now();

        if (ind == -1) {
            return;
        }
        last_frame_ind = ind;

        for (auto eye = 0; eye < 2; eye++) {
            auto ret = av_hwframe_transfer_data(encode_src_color_frames[eye], avvk_color_frames[ind][eye].frame, 0);
            AV_ASSERT_SUCCESS(ret);
//            vulkan::wait_timeline_semaphore(dp->vk_device, avvkframes[ind][eye].vk_frame->sem[0],
//                                            avvkframes[ind][eye].vk_frame->sem_value[0]);
            encode_src_color_frames[eye]->pts = frame_count++;

            if (pass_depth) {
                ret = av_hwframe_transfer_data(encode_src_depth_frames[eye], avvk_depth_frames[ind][eye].frame, 0);
                AV_ASSERT_SUCCESS(ret);
                encode_src_depth_frames[eye]->pts = frame_count++;
            }
        }
//        vulkan::wait_timeline_semaphores(dp->vk_device, {{avvkframes[ind][0].vk_frame->sem[0], avvkframes[ind][0].vk_frame->sem_value[0]},
//                                                         {avvkframes[ind][1].vk_frame->sem[0], avvkframes[ind][1].vk_frame->sem_value[0]}});
        auto copy_end_time = std::chrono::high_resolution_clock::now();

        buffer_pool->post_processing_release_image(ind);
        auto encode_start_time = std::chrono::high_resolution_clock::now();
        for (auto eye = 0; eye < 2; eye++) {
            auto ret = avcodec_send_frame(codec_color_ctx, encode_src_color_frames[eye]);
            if (ret == AVERROR(EAGAIN)) {
                throw std::runtime_error{"FFmpeg encoder returned EAGAIN. Internal buffer full? Try using a higher-end GPU."};
            }
            AV_ASSERT_SUCCESS(ret);

            if (pass_depth) {
                ret = avcodec_send_frame(codec_depth_ctx, encode_src_depth_frames[eye]);
                if (ret == AVERROR(EAGAIN)) {
                    throw std::runtime_error{"FFmpeg encoder returned EAGAIN. Internal buffer full? Try using a higher-end GPU."};
                }
                AV_ASSERT_SUCCESS(ret);
            }
        }

        // receive packets
        for (auto eye = 0; eye < 2; eye++) {
            auto ret = avcodec_receive_packet(codec_color_ctx, encode_out_color_packets[eye]);
            if (ret == AVERROR(EAGAIN)) {
                throw std::runtime_error{"FFmpeg encoder returned EAGAIN when receiving packets. This should never happen."};
            }
            AV_ASSERT_SUCCESS(ret);

            if (pass_depth) {
                ret = avcodec_receive_packet(codec_depth_ctx, encode_out_depth_packets[eye]);
                if (ret == AVERROR(EAGAIN)) {
                    throw std::runtime_error{"FFmpeg encoder returned EAGAIN when receiving packets. This should never happen."};
                }
                AV_ASSERT_SUCCESS(ret);
            }
        }
        auto encode_end_time = std::chrono::high_resolution_clock::now();

        auto copy_time   = std::chrono::duration_cast<std::chrono::microseconds>(copy_end_time - copy_start_time).count();
        auto encode_time = std::chrono::duration_cast<std::chrono::microseconds>(encode_end_time - encode_start_time).count();
        auto acquire_image_time = std::chrono::duration_cast<std::chrono::microseconds>(acquire_image_end_time - acquire_image_start_time).count();
        // print in nano seconds
//        std::cout << frame_count << ": copy time: " << copy_time << " encode time: " << encode_time
//                  << " left size: " << encode_out_color_packets[0]->size << " right size: " << encode_out_color_packets[1]->size
//                  << std::endl;

	
        metrics["copy_time"]   += copy_time;
        metrics["encode_time"] += encode_time;
        metrics["acquire_image_time"] += acquire_image_time;

        enqueue_for_network_send(pose);

        if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - fps_start_time).count() >= 1) {
            log->info("Encoder FPS: {}", fps_counter);
            fps_start_time = std::chrono::high_resolution_clock::now();

            for (auto& metric : metrics) {
                double fps = std::max(fps_counter, (double) 0);
                log->info("{}: {}", metric.first, metric.second / fps);
                metric.second = 0;
            }
            
            if (pass_depth) {
		std::cout << "depth left: " << encode_out_depth_packets[0]->size << " depth right: " << encode_out_depth_packets[0]->size << std::endl;
	}	

            fps_counter = 0;
        } else {
            fps_counter++;
        }
    }

private:
    std::shared_ptr<spdlog::logger>                 log;
    std::shared_ptr<vulkan::display_provider>       dp;
    std::shared_ptr<switchboard>                    sb;
    switchboard::network_writer<compressed_frame>   frames_topic;
    switchboard::reader<fast_pose_type>                  render_pose;
    std::shared_ptr<vulkan::buffer_pool<fast_pose_type>> buffer_pool;
    std::vector<std::array<ffmpeg_vk_frame, 2>>     avvk_color_frames;
    std::vector<std::array<ffmpeg_vk_frame, 2>>     avvk_depth_frames;

    int framerate = 144;
    long bitrate = OFFLOAD_RENDERING_BITRATE;

    bool pass_depth = false;
    bool nalu_only = false;

    AVBufferRef* device_ctx;
    AVBufferRef* cuda_device_ctx;
    AVBufferRef* frame_ctx;
    AVBufferRef* cuda_frame_ctx;

    AVCodecContext*          codec_color_ctx;
    std::array<AVFrame*, 2>  encode_src_color_frames;
    std::array<AVPacket*, 2> encode_out_color_packets;

    AVCodecContext*          codec_depth_ctx;
    std::array<AVFrame*, 2>  encode_src_depth_frames;
    std::array<AVPacket*, 2> encode_out_depth_packets;

    uint64_t frame_count = 0;

    double fps_counter = 0;
    std::chrono::high_resolution_clock::time_point fps_start_time = std::chrono::high_resolution_clock::now();
    std::map<std::string, uint32_t> metrics;

    uint16_t last_frame_ind = -1;

    std::atomic<bool> ready{false};

    void enqueue_for_network_send(fast_pose_type& pose) {
        auto topic_start_time = std::chrono::high_resolution_clock::now();
        uint64_t timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
        if (pass_depth) {
            frames_topic.put(std::make_shared<compressed_frame>(encode_out_color_packets[0], encode_out_color_packets[1], 
                                                                encode_out_depth_packets[0], encode_out_depth_packets[1], pose, timestamp, nalu_only));
        } else {
            frames_topic.put(std::make_shared<compressed_frame>(encode_out_color_packets[0], encode_out_color_packets[1], pose, timestamp, nalu_only));
        }
        auto topic_end_time = std::chrono::high_resolution_clock::now();

        auto topic_put_time = std::chrono::duration_cast<std::chrono::microseconds>(topic_end_time - topic_start_time).count();
//        log->info("topic put time (microseconds): {}", topic_put_time);
        // av_packet_free(&pkt);
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
                // TODO: data race here! need to supply the lock_queue and unlock_queue function.
                // Not yet available in release version of ffmpeg
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

        vulkan_hwdev_ctx->lock_queue   = &ffmpeg_lock_queue;
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
        cuda_hwframe_ctx->sw_format = AV_PIX_FMT_BGRA;
        cuda_hwframe_ctx->width     = buffer_pool->image_pool[0][0].image_info.extent.width;
        cuda_hwframe_ctx->height    = buffer_pool->image_pool[0][0].image_info.extent.height;
        // cuda_hwframe_ctx->initial_pool_size = 0;
        auto ret = av_hwframe_ctx_init(cuda_frame_ref);
        AV_ASSERT_SUCCESS(ret);
        this->cuda_frame_ctx = cuda_frame_ref;
    }

    void ffmpeg_init_buffer_pool() {
        assert(this->buffer_pool != nullptr);
        avvk_color_frames.resize(buffer_pool->image_pool.size());
        avvk_depth_frames.resize(buffer_pool->depth_image_pool.size());
        for (size_t i = 0; i < buffer_pool->image_pool.size(); i++) {
            for (size_t eye = 0; eye < 2; eye++) {
                // Create AVVkFrame
                auto vk_frame = av_vk_frame_alloc();
                if (!vk_frame) {
                    throw std::runtime_error{"Failed to allocate FFmpeg Vulkan frame for color image"};
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
                vk_frame->sem[0]       = vulkan::create_timeline_semaphore(dp->vk_device, 0, &export_semaphore_create_info);
                vk_frame->sem_value[0] = 0;
                vk_frame->layout[0]    = VK_IMAGE_LAYOUT_UNDEFINED;

                avvk_color_frames[i][eye].vk_frame = vk_frame;

                // Create AVFrame
                auto av_frame = av_frame_alloc();
                if (!av_frame) {
                    throw std::runtime_error{"Failed to allocate FFmpeg frame for color image"};
                }
                av_frame->format        = AV_PIX_FMT_VULKAN;
                av_frame->width         = buffer_pool->image_pool[i][eye].image_info.extent.width;
                av_frame->height        = buffer_pool->image_pool[i][eye].image_info.extent.height;
                av_frame->hw_frames_ctx = av_buffer_ref(frame_ctx);
                av_frame->data[0]       = reinterpret_cast<uint8_t*>(vk_frame);
                av_frame->buf[0]        = av_buffer_create(
                    av_frame->data[0], 0, [](void*, uint8_t*) {}, nullptr, 0);
                av_frame->pts            = 0;
                avvk_color_frames[i][eye].frame = av_frame;

                // Do the same for depth if needed
                if (pass_depth) {
                    auto vk_depth_frame = av_vk_frame_alloc();
                    if (!vk_depth_frame) {
                        throw std::runtime_error{"Failed to allocate FFmpeg Vulkan frame for depth image"};
                    }

                    vk_depth_frame->img[0]          = buffer_pool->depth_image_pool[i][eye].image;
                    vk_depth_frame->tiling          = buffer_pool->depth_image_pool[i][eye].image_info.tiling;
                    vk_depth_frame->mem[0]          = buffer_pool->depth_image_pool[i][eye].allocation_info.deviceMemory;
                    vk_depth_frame->size[0]         = buffer_pool->depth_image_pool[i][eye].allocation_info.size;
                    vk_depth_frame->offset[0]       = buffer_pool->depth_image_pool[i][eye].allocation_info.offset;
                    vk_depth_frame->queue_family[0] = dp->queues[vulkan::queue::GRAPHICS].family;

                    // VkExportSemaphoreCreateInfo export_semaphore_create_info{
                    // VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO, nullptr, VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT};
                    vk_depth_frame->sem[0]       = vulkan::create_timeline_semaphore(dp->vk_device, 0, &export_semaphore_create_info);
                    vk_depth_frame->sem_value[0] = 0;
                    vk_depth_frame->layout[0]    = VK_IMAGE_LAYOUT_UNDEFINED;

                    avvk_depth_frames[i][eye].vk_frame = vk_depth_frame;

                    // Create AVFrame
                    auto av_depth_frame = av_frame_alloc();
                    if (!av_depth_frame) {
                        throw std::runtime_error{"Failed to allocate FFmpeg frame for depth image"};
                    }
                    av_depth_frame->format        = AV_PIX_FMT_VULKAN;
                    av_depth_frame->width         = buffer_pool->depth_image_pool[i][eye].image_info.extent.width;
                    av_depth_frame->height        = buffer_pool->depth_image_pool[i][eye].image_info.extent.height;
                    av_depth_frame->hw_frames_ctx = av_buffer_ref(frame_ctx);
                    av_depth_frame->data[0]       = reinterpret_cast<uint8_t*>(vk_depth_frame);
                    av_depth_frame->buf[0]        = av_buffer_create(
                        av_depth_frame->data[0], 0, [](void*, uint8_t*) {}, nullptr, 0);
                    av_depth_frame->pts            = 0;
                    avvk_depth_frames[i][eye].frame = av_depth_frame;
                }
            }
        }

        for (size_t eye = 0; eye < 2; eye++) {
            encode_src_color_frames[eye]                = av_frame_alloc();
            encode_src_color_frames[eye]->format        = AV_PIX_FMT_CUDA;
            encode_src_color_frames[eye]->hw_frames_ctx = av_buffer_ref(cuda_frame_ctx);
            encode_src_color_frames[eye]->width         = buffer_pool->image_pool[0][0].image_info.extent.width;
            encode_src_color_frames[eye]->height        = buffer_pool->image_pool[0][0].image_info.extent.height;
            encode_src_color_frames[eye]->pict_type     = AV_PICTURE_TYPE_I;
            // encode_src_frames[eye]->buf[0] = av_buffer_alloc(1);
            auto ret = av_hwframe_get_buffer(cuda_frame_ctx, encode_src_color_frames[eye], 0);
            AV_ASSERT_SUCCESS(ret);
            // std::cout << encode_src_frames[eye]->data[0] << std::endl;
            // auto ret = av_frame_get_buffer(encode_src_frames[eye], 0);
            // AV_ASSERT_SUCCESS(ret);

            if (pass_depth) {
                encode_src_depth_frames[eye]                = av_frame_alloc();
                encode_src_depth_frames[eye]->format        = AV_PIX_FMT_CUDA;
                encode_src_depth_frames[eye]->hw_frames_ctx = av_buffer_ref(cuda_frame_ctx);
                encode_src_depth_frames[eye]->width         = buffer_pool->depth_image_pool[0][0].image_info.extent.width;
                encode_src_depth_frames[eye]->height        = buffer_pool->depth_image_pool[0][0].image_info.extent.height;
                encode_src_depth_frames[eye]->color_range   = AVCOL_RANGE_JPEG;
                encode_src_depth_frames[eye]->pict_type     = AV_PICTURE_TYPE_I;
                ret = av_hwframe_get_buffer(cuda_frame_ctx, encode_src_depth_frames[eye], 0);
                AV_ASSERT_SUCCESS(ret);
            }
        }
    }

    void ffmpeg_init_encoder() {
        auto encoder = avcodec_find_encoder_by_name(OFFLOAD_RENDERING_FFMPEG_ENCODER_NAME);
        if (!encoder) {
            throw std::runtime_error{"Failed to find FFmpeg encoder"};
        }

        this->codec_color_ctx = avcodec_alloc_context3(encoder);
        if (!codec_color_ctx) {
            throw std::runtime_error{"Failed to allocate FFmpeg encoder context for color images"};
        }

        codec_color_ctx->thread_count = 0; // auto
        codec_color_ctx->thread_type  = FF_THREAD_SLICE;

        codec_color_ctx->pix_fmt       = AV_PIX_FMT_CUDA; // alpha channel will be useful later for compositing
        codec_color_ctx->sw_pix_fmt    = AV_PIX_FMT_BGRA;
        codec_color_ctx->hw_frames_ctx = av_buffer_ref(cuda_frame_ctx);
        codec_color_ctx->width         = buffer_pool->image_pool[0][0].image_info.extent.width;
        codec_color_ctx->height        = buffer_pool->image_pool[0][0].image_info.extent.height;
        codec_color_ctx->time_base     = {1, framerate}; // 90 fps
        codec_color_ctx->framerate     = {framerate, 1};
        codec_color_ctx->bit_rate      = bitrate;

        // Set zero latency
        codec_color_ctx->max_b_frames = 0;
        codec_color_ctx->gop_size     = 0; // intra-only for now
        av_opt_set_int(codec_color_ctx->priv_data, "zerolatency", 1, 0);
        av_opt_set_int(codec_color_ctx->priv_data, "delay", 0, 0);

        // Set encoder profile
        // av_opt_set(codec_ctx->priv_data, "profile", "high", 0);

        // Set encoder preset
        // av_opt_set(codec_ctx->priv_data, "preset", "fast", 0);

        auto ret = avcodec_open2(codec_color_ctx, encoder, nullptr);
        AV_ASSERT_SUCCESS(ret);

        if (pass_depth) {
            this->codec_depth_ctx = avcodec_alloc_context3(encoder);
            if (!codec_depth_ctx) {
                throw std::runtime_error{"Failed to allocate FFmpeg encoder context for color images"};
            }

            codec_depth_ctx->thread_count = 0; // auto
            codec_depth_ctx->thread_type  = FF_THREAD_SLICE;

            codec_depth_ctx->pix_fmt       = AV_PIX_FMT_CUDA; // alpha channel will be useful later for compositing
            codec_depth_ctx->sw_pix_fmt    = AV_PIX_FMT_BGRA;
            codec_depth_ctx->hw_frames_ctx = av_buffer_ref(cuda_frame_ctx);
            codec_depth_ctx->width         = buffer_pool->depth_image_pool[0][0].image_info.extent.width;
            codec_depth_ctx->height        = buffer_pool->depth_image_pool[0][0].image_info.extent.height;
            codec_depth_ctx->time_base     = {1, framerate}; // 90 fps
            codec_depth_ctx->framerate     = {framerate, 1};
            codec_depth_ctx->bit_rate      = bitrate; // 10 Mbps
            codec_depth_ctx->color_range   = AVCOL_RANGE_JPEG;

            // Set lossless encoding using AVOptions
//            av_opt_set_int(codec_depth_ctx->priv_data, "lossless", 1, 0);

            // Set zero latency
            codec_depth_ctx->max_b_frames = 0;
            codec_depth_ctx->gop_size     = 0; // intra-only for now
            av_opt_set_int(codec_depth_ctx->priv_data, "zerolatency", 1, 0);
            av_opt_set_int(codec_depth_ctx->priv_data, "delay", 0, 0);

            // Set encoder profile
            // av_opt_set(codec_ctx->priv_data, "profile", "high", 0);

            // Set encoder preset
            // av_opt_set(codec_ctx->priv_data, "preset", "fast", 0);

            ret = avcodec_open2(codec_depth_ctx, encoder, nullptr);
            AV_ASSERT_SUCCESS(ret);
        }
    }

};

class offload_rendering_server_loader
    : public plugin
    , public vulkan::vk_extension_request {
public:
    offload_rendering_server_loader(const std::string& name, phonebook* pb)
        : plugin(name, pb)
        , offload_rendering_server_plugin{std::make_shared<offload_rendering_server>(name, pb)} {
        pb->register_impl<vulkan::timewarp>(offload_rendering_server_plugin);
        pb->register_impl<pose_prediction>(offload_rendering_server_plugin);
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
        offload_rendering_server_plugin->start();
    }

    void stop() override {
        offload_rendering_server_plugin->stop();
    }

private:
    std::shared_ptr<offload_rendering_server> offload_rendering_server_plugin;
};

PLUGIN_MAIN(offload_rendering_server_loader)
