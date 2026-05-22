#ifdef __ANDROID__

#    include "stereo_surface_decoder.hpp"

#    include <spdlog/spdlog.h>

using namespace ILLIXR;
using namespace ILLIXR::data_format;

stereo_surface_decoder::stereo_surface_decoder(int width, int height, bool is_depth, bool is_10bit)
    : original_width_{width}
    , original_height_{height}
    , padded_width_{(width + 31) & ~31} // H.265 requires 32-pixel alignment
    , padded_height_{(height + 31) & ~31}
    , is_depth_{is_depth}
    , is_10bit_{is_10bit} {
#    ifdef COMBINED_ENCODING
    // The combined bitstream has both eyes side by side, so the decoder is
    // created at twice the per-eye padded width.  padded_width_ always stores
    // the per-eye value so the rest of the class stays consistent.
    const int combined_w = padded_width_ * 2;
    combined_decoder_    = std::make_unique<frame_decoder>(
        /*eye_index=*/0, combined_w, padded_height_, is_10bit_);
    spdlog::get("illixr")->info("[stereo_surface_decoder] COMBINED_ENCODING: created {} {} combined decoder "
                                "{}x{} (per-eye padded {}x{}, original {}x{})",
                                is_10bit_ ? "10-bit" : "8-bit", is_depth_ ? "DEPTH" : "COLOR", combined_w, padded_height_,
                                padded_width_, padded_height_, original_width_, original_height_);
#    else
    left_decoder_  = std::make_unique<frame_decoder>(0, padded_width_, padded_height_, is_10bit_);
    right_decoder_ = std::make_unique<frame_decoder>(1, padded_width_, padded_height_, is_10bit_);
    spdlog::get("illixr")->info("[stereo_surface_decoder] Created {} {} decoder {}x{} (padded from {}x{})",
                                is_10bit_ ? "10-bit" : "8-bit", is_depth_ ? "DEPTH" : "COLOR", padded_width_, padded_height_,
                                original_width_, original_height_);
#    endif // COMBINED_ENCODING
}

stereo_surface_decoder::~stereo_surface_decoder() {
    stop();
}

bool stereo_surface_decoder::initialize() {
#    ifdef COMBINED_ENCODING
    if (!combined_decoder_->initialize()) {
        spdlog::get("illixr")->error("[stereo_surface_decoder] Combined decoder init failed");
        return false;
    }
    spdlog::get("illixr")->info("[stereo_surface_decoder] COMBINED_ENCODING initialized (AImageReader/Vulkan path)");
    return true;
#    else
    if (!left_decoder_->initialize()) {
        spdlog::get("illixr")->error("[stereo_surface_decoder] Left decoder init failed");
        return false;
    }

    if (!right_decoder_->initialize()) {
        spdlog::get("illixr")->error("[stereo_surface_decoder] Right decoder init failed");
        left_decoder_->stop();
        return false;
    }

    spdlog::get("illixr")->info("[stereo_surface_decoder] Initialized (AImageReader/Vulkan path)");
    return true;
#    endif // COMBINED_ENCODING
}

bool stereo_surface_decoder::queue_encoded_data(int eye, const uint8_t* data, size_t size, int64_t timestamp_us,
                                                bool is_keyframe, uint64_t frame_number) {
#    ifdef COMBINED_ENCODING
    // The combined bitstream is sent as eye=0.  The right-eye slot is empty
    // on the wire under COMBINED_ENCODING, so eye=1 calls are no-ops.
    if (eye != 0) {
        return true;
    }
    return combined_decoder_->queue_encoded_data(data, size, timestamp_us, is_keyframe, frame_number);
#    else
    if (eye == 0) {
        return left_decoder_->queue_encoded_data(data, size, timestamp_us, is_keyframe, frame_number);
    }
    if (eye == 1) {
        return right_decoder_->queue_encoded_data(data, size, timestamp_us, is_keyframe, frame_number);
    }
    return false;
#    endif // COMBINED_ENCODING
}

dual_frames stereo_surface_decoder::get_current_frame(time_point render_time) {
#    ifdef COMBINED_ENCODING
    // The combined decoder produces a single wide AHardwareBuffer containing
    // both eyes side by side (total width = padded_width_ * 2).
    // Return it in both left_eye and right_eye so stereo_renderer::receive_frame
    // can import it once.  render_eye() uses u_offset push constants to sample
    // the correct half.  No splitter is needed.
    if (!combined_decoder_->has_decoded_frames()) {
        static uint64_t wait_count = 0;
        if (++wait_count % 1000 == 1) {
            spdlog::get("illixr")->info("[stereo_surface_decoder][{}][combined] Waiting for first decoded frame "
                                        "(released={}, queued={}, dropped={}, count={})",
                                        is_depth_ ? "depth" : "color", combined_decoder_->get_frames_released(),
                                        combined_decoder_->get_frames_queued(), combined_decoder_->get_frames_dropped(),
                                        wait_count);
        }
        return dual_frames{};
    }

    AHardwareBuffer* combined_buf        = nullptr;
    uint64_t         frame_number        = 0;
    std::tie(combined_buf, frame_number) = combined_decoder_->acquire_latest_buffer();
    if (combined_buf == nullptr) {
        return dual_frames{};
    }

    // Acquire a second reference so dual_frames can release each eye independently.
    AHardwareBuffer_acquire(combined_buf);

    // Report the combined width so stereo_renderer knows the true image extent.
    const int combined_width = padded_width_ * 2;
    return dual_frames{render_time, combined_buf, combined_buf, combined_width, padded_height_, frame_number};

#    else
    // Default path: acquire from the two independent per-eye decoders.
    if (!left_decoder_->has_decoded_frames() || !right_decoder_->has_decoded_frames()) {
        static uint64_t wait_count = 0;
        if (++wait_count % 100 == 1) {
            spdlog::get("illixr")->info("[stereo_surface_decoder][{}] Waiting for first decoded frames "
                                        "(left_decoded={}, right_decoded={}, "
                                        "left_released={}, right_released={}, "
                                        "left_queued={}, right_queued={}, "
                                        "left_dropped={}, right_dropped={}, count={})",
                                        is_depth_ ? "depth" : "color", left_decoder_->get_frames_decoded(),
                                        right_decoder_->get_frames_decoded(), left_decoder_->get_frames_released(),
                                        right_decoder_->get_frames_released(), left_decoder_->get_frames_queued(),
                                        right_decoder_->get_frames_queued(), left_decoder_->get_frames_dropped(),
                                        right_decoder_->get_frames_dropped(), wait_count);
        }
        return dual_frames{};
    }

    AHardwareBuffer* left_buf        = nullptr;
    AHardwareBuffer* right_buf       = nullptr;
    uint64_t         frame_number    = 0;
    std::tie(left_buf, frame_number) = left_decoder_->acquire_latest_buffer();
    std::tie(right_buf, std::ignore) = right_decoder_->acquire_latest_buffer();

    static uint64_t acquire_count = 0;
    acquire_count++;
    if (acquire_count <= 5 || acquire_count % 200 == 0) {
        spdlog::get("illixr")->info("[stereo_surface_decoder][{}] acquire #{}: "
                                    "left={} right={} frame_number={}",
                                    is_depth_ ? "depth" : "color", acquire_count, static_cast<void*>(left_buf),
                                    static_cast<void*>(right_buf), frame_number);
    }

    if (left_buf == nullptr || right_buf == nullptr) {
        if (left_buf)
            AHardwareBuffer_release(left_buf);
        if (right_buf)
            AHardwareBuffer_release(right_buf);
        return dual_frames{};
    }

    return dual_frames{render_time, left_buf, right_buf, padded_width_, padded_height_, frame_number};
#    endif // COMBINED_ENCODING
}

void stereo_surface_decoder::release_frame(const dual_frames& frame) {
    if (frame.format != data_format::frame_format::hardware_buffer) {
        return;
    }
    if (frame.left_eye.hw_buffer != nullptr) {
        AHardwareBuffer_release(frame.left_eye.hw_buffer);
    }
    if (frame.right_eye.hw_buffer != nullptr) {
        AHardwareBuffer_release(frame.right_eye.hw_buffer);
    }
}

// ============================================================================
// Lifecycle
// ============================================================================

bool stereo_surface_decoder::is_ready() const {
#    ifdef COMBINED_ENCODING
    return combined_decoder_ && combined_decoder_->is_ready();
#    else
    return left_decoder_->is_ready() && right_decoder_->is_ready();
#    endif
}

// ============================================================================
// Timing statistics
// ============================================================================

stereo_decode_timing_stats stereo_surface_decoder::get_and_reset_timing_stats() {
    stereo_decode_timing_stats stats;
#    ifdef COMBINED_ENCODING
    if (combined_decoder_) {
        // Report the combined decoder's stats in both left_eye and right_eye
        // fields so the calling code (log_android_decode_timing) can remain
        // unchanged; only the left_eye values will be meaningful.
        stats.left_eye  = combined_decoder_->get_and_reset_timing_stats();
        stats.right_eye = decode_timing_stats{};
    }
#    else
    if (left_decoder_) {
        stats.left_eye = left_decoder_->get_and_reset_timing_stats();
    }
    if (right_decoder_) {
        stats.right_eye = right_decoder_->get_and_reset_timing_stats();
    }
#    endif
    return stats;
}

stereo_decode_timing_stats stereo_surface_decoder::get_timing_stats() const {
    stereo_decode_timing_stats stats;
#    ifdef COMBINED_ENCODING
    if (combined_decoder_) {
        stats.left_eye  = combined_decoder_->get_timing_stats();
        stats.right_eye = decode_timing_stats{};
    }
#    else
    if (left_decoder_) {
        stats.left_eye = left_decoder_->get_timing_stats();
    }
    if (right_decoder_) {
        stats.right_eye = right_decoder_->get_timing_stats();
    }
#    endif
    return stats;
}

void stereo_surface_decoder::flush() {
#    ifdef COMBINED_ENCODING
    if (combined_decoder_) {
        combined_decoder_->flush();
    }
#    else
    left_decoder_->flush();
    right_decoder_->flush();
#    endif
}

void stereo_surface_decoder::stop() {
#    ifdef COMBINED_ENCODING
    if (combined_decoder_)
        combined_decoder_->stop();
#    else
    if (left_decoder_)
        left_decoder_->stop();
    if (right_decoder_)
        right_decoder_->stop();
#    endif
}

#endif // __ANDROID__
