//
// Created by steven on 10/22/23.
//

#ifndef ILLIXR_FFMPEG_UTILS_HPP
#define ILLIXR_FFMPEG_UTILS_HPP

#include "vulkan/vulkan.h"

#include <optional>
#include <utility>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_vulkan.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
}

#define OFFLOAD_RENDERING_FFMPEG_ENCODER_NAME "h264_nvenc"
#define OFFLOAD_RENDERING_BITRATE             100000000

namespace ILLIXR::vulkan::ffmpeg_utils {

static std::weak_ptr<vulkan::display_provider> display_provider_ffmpeg;

void ffmpeg_lock_queue(struct AVHWDeviceContext* ctx, uint32_t queue_family, uint32_t index) {
    if (auto dp = display_provider_ffmpeg.lock()) {
        std::optional<vulkan::queue> queue;
        for (auto& q : dp->queues) {
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

void ffmpeg_unlock_queue(struct AVHWDeviceContext* ctx, uint32_t queue_family, uint32_t index) {
    if (auto dp = display_provider_ffmpeg.lock()) {
        std::optional<vulkan::queue> queue;
        for (auto& q : dp->queues) {
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

std::optional<AVPixelFormat> get_pix_format_from_vk_format(VkFormat format) {
    for (int fmt = AV_PIX_FMT_NONE; fmt < AV_PIX_FMT_NB; fmt++) {
        auto vk_fmt = av_vkfmt_from_pixfmt(static_cast<AVPixelFormat>(fmt));
        if (vk_fmt && *vk_fmt == format) {
            return static_cast<AVPixelFormat>(fmt);
        }
    }
    return std::nullopt;
}

void AV_ASSERT_SUCCESS(int ret) {
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
        throw std::runtime_error{std::string{"FFmpeg error: "} + errbuf};
    }
}

struct ffmpeg_vk_frame {
    AVFrame*   frame = nullptr;
    AVVkFrame* vk_frame = nullptr;
};
} // namespace ILLIXR::vulkan::ffmpeg_utils

#endif // ILLIXR_FFMPEG_UTILS_HPP
