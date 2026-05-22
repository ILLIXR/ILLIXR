#pragma once

#ifdef __ANDROID__
#include "illixr/data_format/frame.hpp"

#include "frame_decoder.hpp"

#include <android/hardware_buffer.h>
#include <memory>
#include <utility>

namespace ILLIXR {
/**
 * @brief Aggregated timing statistics for stereo decoding.
 *
 * Combines timing from both left and right eye decoders.
 */
struct stereo_decode_timing_stats {
    decode_timing_stats left_eye;
    decode_timing_stats right_eye;

    [[nodiscard]] [[maybe_unused]] double total_avg_decode_time_ms() const {
        double left_avg = left_eye.avg_decode_time_us() / 1000.0;
        double right_avg = right_eye.avg_decode_time_us() / 1000.0;
        // Return the max since both eyes decode in parallel
        return std::max(left_avg, right_avg);
    }

    [[nodiscard]] [[maybe_unused]] double total_avg_queue_time_ms() const {
        return (left_eye.avg_queue_time_us() + right_eye.avg_queue_time_us()) / 2000.0;
    }

    [[nodiscard]] uint64_t total_frames() const {
        return left_eye.frame_count + right_eye.frame_count;
    }
};

/**
 * @brief Stereo decoder that manages left and right eye frame_decoders.
 *
 * Default path: one frame_decoder per eye.
 *
 * COMBINED_ENCODING path: one frame_decoder at (per-eye width * 2) decodes
 * the side-by-side bitstream. get_current_frame() returns the combined
 * AHardwareBuffer in both left_eye and right_eye; stereo_renderer samples
 * each eye's horizontal half via u_offset push constants in the vertex shader.
 * No splitter or separate Vulkan context is needed.
 *
 * NOTE: When used for depth, the NV12 output contains packed depth bytes
 * rather than YUV; the application uses appropriate shaders to unpack.
 */
class stereo_surface_decoder {
public:
    /**
     * @brief Create a stereo decoder for either color or depth streams.
     *
     * For color: Decodes HEVC color → NV12 YUV → rendered as RGB
     * For depth: Decodes HEVC RG depth → NV12 (R in Y plane, G in UV) → unpacked to 16-bit depth
     * @param width     Per-eye frame width (before H.265 alignment padding).
     *                  Under COMBINED_ENCODING the internal decoder is created
     *                  at twice this value; the public interface always uses
     *                  the per-eye width.
     * @param height    Frame height (before padding).
     * @param is_depth  True for depth streams.
     * @param is_10bit  True for HEVC Main 10 (10-bit) bitstreams.
     * @param is_10bit  True for HEVC Main 10 (10-bit) bitstreams, e.g. the
     *                  motion-vector stream.  Passes the flag through to each
     *                  frame_decoder so MediaCodec selects the correct profile.
     * NOTE: When used for depth, the NV12 output contains packed depth bytes (R=high, G=low)
     * rather than actual YUV color data. The application must use appropriate shaders to
     * unpack the depth values.
     */
    stereo_surface_decoder(int width, int height, bool is_depth = false, bool is_10bit = false);

    ~stereo_surface_decoder();

    // Non-copyable
    stereo_surface_decoder(const stereo_surface_decoder&)            = delete;
    stereo_surface_decoder& operator=(const stereo_surface_decoder&) = delete;

    /**
     * @brief Initialize the decoder(s) and AImageReader/MediaCodec pipelines.
     */
    bool initialize();

    /**
     * @brief Queue encoded data for decoding.
     *
     * @param eye          0 = left, 1 = right.
     *                     Under COMBINED_ENCODING eye=1 is a no-op; the entire
     *                     combined bitstream arrives with eye=0.
     * @param data         Pointer to the encoded NAL unit data.
     * @param size         Byte count.
     * @param timestamp_us PTS in microseconds.
     * @param is_keyframe  True if this is an IDR frame.
     * @param frame_number Monotonic frame counter.
     */
    bool queue_encoded_data(int eye, const uint8_t* data, size_t size,
                            int64_t timestamp_us, bool is_keyframe, uint64_t frame_number);

    /**
     * @brief Acquire the latest decoded frame pair.
     *
     * Returns dual_frames with format = hardware_buffer, or an invalid
     * dual_frames if no decoded output is available yet.
     *
     * The caller must call release_frame() when the GPU is done reading the buffers.
     *
     * @param render_time  Timestamp for this frame.
     */
    [[nodiscard]] data_format::dual_frames get_current_frame(time_point render_time);

    /**
     * @brief Release AHardwareBuffers acquired by get_current_frame().
     *
     * Must be called after the Vulkan submission that reads these buffers has
     * finished (e.g. after a VkFence or VkSemaphore signals completion).
     */
    void release_frame(const data_format::dual_frames& frame);

    // Check if both decoders are ready
    [[nodiscard]] bool is_ready() const;

    // Flush both decoders
    void flush();

    // Stop both decoders
    void stop();

    // Get frame dimensions
    [[nodiscard]] int get_width() const { return padded_width_; }

    [[maybe_unused]] [[nodiscard]] int get_height() const { return padded_height_; }
    /**
     * @brief Get timing statistics from both decoders and reset counters.
     *
     * @return Aggregated timing stats from left and right eye decoders.
     */
    stereo_decode_timing_stats get_and_reset_timing_stats();

    /**
     * @brief Get timing statistics without resetting.
     *
     * @return Current timing statistics snapshot.
     */
    [[nodiscard]] stereo_decode_timing_stats get_timing_stats() const;

    // Diagnostic counters — under COMBINED_ENCODING the combined decoder's
    // values are returned for both eyes to keep calling code uniform.
    [[nodiscard]] uint64_t get_left_frames_decoded() const {
#ifdef COMBINED_ENCODING
        return combined_decoder_ ? combined_decoder_->get_frames_decoded() : 0;
#else
        return left_decoder_ ? left_decoder_->get_frames_decoded() : 0;
#endif
    }

    [[nodiscard]] uint64_t get_right_frames_decoded() const {
#ifdef COMBINED_ENCODING
        return combined_decoder_ ? combined_decoder_->get_frames_decoded() : 0;
#else
        return right_decoder_ ? right_decoder_->get_frames_decoded() : 0;
#endif
    }

    [[nodiscard]] uint64_t get_left_frames_dropped() const {
#ifdef COMBINED_ENCODING
        return combined_decoder_ ? combined_decoder_->get_frames_dropped() : 0;
#else
        return left_decoder_ ? left_decoder_->get_frames_dropped() : 0;
#endif
    }

    [[nodiscard]] uint64_t get_right_frames_dropped() const {
#ifdef COMBINED_ENCODING
        return combined_decoder_ ? combined_decoder_->get_frames_dropped() : 0;
#else
        return right_decoder_ ? right_decoder_->get_frames_dropped() : 0;
#endif
    }

    [[nodiscard]] size_t get_left_queue_depth() const {
#ifdef COMBINED_ENCODING
        return combined_decoder_ ? combined_decoder_->get_queue_depth() : 0;
#else
        return left_decoder_ ? left_decoder_->get_queue_depth() : 0;
#endif
    }

    [[nodiscard]] size_t get_right_queue_depth() const {
        // Under COMBINED_ENCODING both eyes share one decoder; report the same
        // queue depth for both so the receiver_loop drop logic is consistent.
#ifdef COMBINED_ENCODING
        return combined_decoder_ ? combined_decoder_->get_queue_depth() : 0;
#else
        return right_decoder_ ? right_decoder_->get_queue_depth() : 0;
#endif
    }

    [[nodiscard]] bool is_depth_decoder() const { return is_depth_; }

    friend class offload_rendering_client;
private:
    int original_width_;
    int original_height_;
    int padded_width_;    ///< Per-eye padded width
    int padded_height_;

    bool is_depth_;
    bool is_10bit_;

#ifdef COMBINED_ENCODING
    /// Single decoder whose internal width = padded_width_ * 2.
    std::unique_ptr<frame_decoder> combined_decoder_;
#else
    std::unique_ptr<frame_decoder> left_decoder_;
    std::unique_ptr<frame_decoder> right_decoder_;
#endif // COMBINED_ENCODING
};
} // namespace ILLIXR

#endif // __ANDROID__
