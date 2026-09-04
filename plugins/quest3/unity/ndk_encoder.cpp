#include "ndk_encoder.hpp"

#include <spdlog/spdlog.h>

using namespace ILLIXR;

ndk_encoder::ndk_encoder(int32_t width, int32_t height, int32_t bps, uint8_t fps)
    : width_{width}
    , height_{height}
    , capture_fps_{fps}
    , bitrate_bps_{bps} { }

bool ndk_encoder::initialize(uint8_t iframe_rate) {
    codec_ = AMediaCodec_createEncoderByType(HEVC_MIME);
    if (codec_ == nullptr) {
        spdlog::get("illixr")->error("AMediaCodec_createEncoderByType failed");
        return false;
    }

    codec_format_ = AMediaFormat_new();
    AMediaFormat_setString(codec_format_, AMEDIAFORMAT_KEY_MIME, HEVC_MIME);
    AMediaFormat_setInt32(codec_format_, AMEDIAFORMAT_KEY_WIDTH, width_);
    AMediaFormat_setInt32(codec_format_, AMEDIAFORMAT_KEY_HEIGHT, height_);
    AMediaFormat_setInt32(codec_format_, AMEDIAFORMAT_KEY_BIT_RATE, bitrate_bps_);
    AMediaFormat_setInt32(codec_format_, AMEDIAFORMAT_KEY_FRAME_RATE, static_cast<int32_t>(capture_fps_));
    AMediaFormat_setInt32(codec_format_, AMEDIAFORMAT_KEY_I_FRAME_INTERVAL, iframe_rate);
    // COLOR_FormatSurface — required for Surface-input mode.
    AMediaFormat_setInt32(codec_format_, AMEDIAFORMAT_KEY_COLOR_FORMAT, 0x7F000789);

    // Explicitly signal full-range BT.601 to match Quest 3 Camera2 HAL output.
    // Without this the encoder leaves color aspects unspecified in the SPS VUI,
    // causing decoders to guess — NVDEC defaults to limited-range and crushes
    // blacks even when the input is full-range.
    AMediaFormat_setInt32(codec_format_, AMEDIAFORMAT_KEY_COLOR_STANDARD, 2); // BT601_625
    AMediaFormat_setInt32(codec_format_, AMEDIAFORMAT_KEY_COLOR_RANGE, 1);    // full
    AMediaFormat_setInt32(codec_format_, AMEDIAFORMAT_KEY_COLOR_TRANSFER, 3); // SDR

    if (AMediaCodec_configure(codec_, codec_format_, nullptr, nullptr, 1) != AMEDIA_OK) {
        spdlog::get("illixr")->error("AMediaCodec_configure failed");
        destroy_encoder();
        return false;
    }

    if (AMediaCodec_createInputSurface(codec_, &encoder_window_) != AMEDIA_OK || encoder_window_ == nullptr) {
        spdlog::get("illixr")->error("AMediaCodec_createInputSurface failed");
        destroy_encoder();
        return false;
    }

    if (AMediaCodec_start(codec_) != AMEDIA_OK) {
        spdlog::get("illixr")->error("AMediaCodec_start failed");
        destroy_encoder();
        return false;
    }

    spdlog::get("illixr")->info("MediaCodec HEVC encoder started (Surface-input): {}x{}", width_, height_);
    return true;
}

void ndk_encoder::destroy_encoder() {
    if (codec_ != nullptr) {
        AMediaCodec_stop(codec_);
        AMediaCodec_delete(codec_);
        codec_ = nullptr;
    }
    encoder_window_ = nullptr; // owned by codec
    if (codec_format_ != nullptr) {
        AMediaFormat_delete(codec_format_);
        codec_format_ = nullptr;
    }
}

void ndk_encoder::drain_encoder_output(int64_t offset) {
    if (codec_ == nullptr)
        return;

    while (true) {
        AMediaCodecBufferInfo info{};
        ssize_t               idx = AMediaCodec_dequeueOutputBuffer(codec_, &info, CODEC_TIMEOUT_US);

        if (idx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED)
            continue;
        if (idx == AMEDIACODEC_INFO_TRY_AGAIN_LATER || idx < 0)
            break;

        if (info.size > 0) {
            size_t         buf_size = 0;
            const uint8_t* buf      = AMediaCodec_getOutputBuffer(codec_, static_cast<size_t>(idx), &buf_size);
            if (buf != nullptr) {
                const bool     is_config = (info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) != 0;
                const uint8_t* data      = buf + info.offset;
                const size_t   size      = static_cast<size_t>(info.size);

                if (is_config) {
                    // Cache SPS/PPS — prepend to the next IDR frame so NVDEC
                    // on the server receives a self-contained access unit.
                    sps_pps_cache_.assign(data, data + size);
                    spdlog::get("illixr")->info("[encoder] cached SPS/PPS {}B", size);
                } else {
                    pending_rgb frame{};
                    // Prepend cached SPS/PPS if present so the server's NVDEC
                    // can initialize from the first packet it receives.
                    if (!sps_pps_cache_.empty()) {
                        frame.encoded.reserve(sps_pps_cache_.size() + size);
                        frame.encoded.insert(frame.encoded.end(), sps_pps_cache_.begin(), sps_pps_cache_.end());
                        sps_pps_cache_.clear();
                    }
                    frame.encoded.insert(frame.encoded.end(), data, data + size);
                    frame.timestamp = static_cast<XrTime>(info.presentationTimeUs * 1'000LL + offset);
                    spdlog::get("illixr")->debug("[encoder] drained frame ts={}us size={}B flags=0x{:X}",
                                                 info.presentationTimeUs, frame.encoded.size(), info.flags);
                    pending_frames_.push_back(std::move(frame));
                }
            }
        }
        AMediaCodec_releaseOutputBuffer(codec_, static_cast<size_t>(idx), false);
    }
}
