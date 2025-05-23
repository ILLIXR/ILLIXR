#pragma once

#include "vulkan/vulkan.h"

#include <optional>
#include <utility>
extern "C" {
#include "libavcodec_illixr/avcodec.h"
#include "libavformat_illixr/avformat.h"
#include "libavutil_illixr/hwcontext.h"
#include "libavutil_illixr/hwcontext_vulkan.h"
#include "libavutil_illixr/opt.h"
#include "libavutil_illixr/pixdesc.h"
}

#define OFFLOAD_RENDERING_FFMPEG_ENCODER_NAME "hevc_nvenc"
#define OFFLOAD_RENDERING_COLOR_BITRATE       90000000
#define OFFLOAD_RENDERING_DEPTH_BITRATE       5000000

namespace ILLIXR::vulkan::ffmpeg_utils {

static std::weak_ptr<vulkan::display_provider> display_provider_ffmpeg;

static void ffmpeg_lock_queue(struct AVHWDeviceContext* ctx, uint32_t queue_family, uint32_t index) {
    (void) ctx;
    (void) index;
    if (auto dp = display_provider_ffmpeg.lock()) {
        std::optional<vulkan::queue> queue;
        for (auto& q : dp->queues_) {
            if (q.second.family == queue_family) {
                queue = q.second;
                break;
            }
        }
        if (!queue) {
            throw std::runtime_error{"Failed to find queue with family " + std::to_string(queue_family)};
        }
        queue->mutex->lock();
    } else {
        throw std::runtime_error{"Weak pointer to display_provider is expired"};
    }
}

static void ffmpeg_unlock_queue(struct AVHWDeviceContext* ctx, uint32_t queue_family, uint32_t index) {
    (void) ctx;
    (void) index;
    if (auto dp = display_provider_ffmpeg.lock()) {
        std::optional<vulkan::queue> queue;
        for (auto& q : dp->queues_) {
            if (q.second.family == queue_family) {
                queue = q.second;
                break;
            }
        }
        if (!queue) {
            throw std::runtime_error{"Failed to find queue with family " + std::to_string(queue_family)};
        }
        queue->mutex->unlock();
    } else {
        throw std::runtime_error{"Weak pointer to display_provider is expired"};
    }
}

static std::optional<AVPixelFormat> get_pix_format_from_vk_format(VkFormat format) {
    for (int fmt = AV_PIX_FMT_NONE; fmt < AV_PIX_FMT_NB; fmt++) {
        auto vk_fmt = av_vkfmt_from_pixfmt(static_cast<AVPixelFormat>(fmt));
        if (vk_fmt && *vk_fmt == format) {
            return static_cast<AVPixelFormat>(fmt);
        }
    }
    return std::nullopt;
}

static void AV_ASSERT_SUCCESS(int ret) {
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
        throw std::runtime_error{std::string{"FFmpeg error: "} + errbuf};
    }
}

struct ffmpeg_vk_frame {
    AVFrame*   frame    = nullptr;
    AVVkFrame* vk_frame = nullptr;
};
} // namespace ILLIXR::vulkan::ffmpeg_utils
