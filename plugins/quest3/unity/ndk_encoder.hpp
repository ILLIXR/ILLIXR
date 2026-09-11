#pragma once

#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <openxr/openxr.h>
#include <vector>

static constexpr const char* HEVC_MIME        = "video/hevc";
static constexpr int64_t     CODEC_TIMEOUT_US = 0LL; // non-blocking drain

namespace ILLIXR {
// ---- Pending encoded RGB frames ----
struct pending_rgb {
    std::vector<uint8_t> encoded;
    XrTime               timestamp = 0;
};

class ndk_encoder {
public:
    ndk_encoder(int32_t width, int32_t height, int32_t bps, uint8_t fps = 2);
    bool initialize(uint8_t iframe_rate);
    void destroy_encoder();
    // ---- MediaCodec HEVC encoder (Surface-input mode) ----
    void          drain_encoder_output(int64_t offset);
    const int32_t width_;
    const int32_t height_;
    const uint8_t capture_fps_;

    [[nodiscard]] ANativeWindow* get_window() const {
        return encoder_window_;
    }

    std::vector<pending_rgb> pending_frames_;

private:
    ANativeWindow* encoder_window_ = nullptr;
    AMediaCodec*   codec_          = nullptr;
    AMediaFormat*  codec_format_   = nullptr;

    int32_t bitrate_bps_ = 5'000'000;

    std::vector<uint8_t> sps_pps_cache_; // SPS/PPS prepended to next IDR
};
} // namespace ILLIXR
