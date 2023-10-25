//
// Created by steven on 10/22/23.
//

#ifndef ILLIXR_FFMPEG_UTILS_HPP
#define ILLIXR_FFMPEG_UTILS_HPP

#include "vulkan/vulkan.h"

#include <optional>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_vulkan.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
}

#define OFFLOAD_RENDERING_FFMPEG_ENCODER_NAME "h264_nvenc"

namespace ILLIXR::vulkan::ffmpeg_utils {
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
    AVFrame * frame;
    AVVkFrame * vk_frame;
};
}

#endif // ILLIXR_FFMPEG_UTILS_HPP
