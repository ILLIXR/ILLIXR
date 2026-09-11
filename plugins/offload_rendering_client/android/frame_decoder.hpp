#pragma once

#ifdef __ANDROID__
#    include <android/hardware_buffer.h>
#    include <atomic>
#    include <chrono>
#    include <condition_variable>
#    include <cstdint>
#    include <deque>
#    include <media/NdkImageReader.h>
#    include <media/NdkMediaCodec.h>
#    include <mutex>
#    include <queue>
#    include <thread>
#    include <unordered_map>
#    include <vector>

namespace ILLIXR {

/**
 * @brief Timing statistics for decode operations.
 *
 * All times are in microseconds.
 */
struct decode_timing_stats {
    uint64_t total_queue_time_us{0};        // Time spent queueing input data
    uint64_t total_decode_time_us{0};       // Time from input queued to output available
    uint64_t total_output_drain_time_us{0}; // Time spent draining output buffers
    uint64_t min_decode_latency_us{UINT64_MAX};
    uint64_t max_decode_latency_us{0};
    uint64_t frame_count{0};

    void reset() {
        total_queue_time_us        = 0;
        total_decode_time_us       = 0;
        total_output_drain_time_us = 0;
        min_decode_latency_us      = UINT64_MAX;
        max_decode_latency_us      = 0;
        frame_count                = 0;
    }

    [[nodiscard]] [[maybe_unused]] double avg_queue_time_us() const {
        return frame_count > 0 ? static_cast<double>(total_queue_time_us) / frame_count : 0.0;
    }

    [[nodiscard]] [[maybe_unused]] double avg_decode_time_us() const {
        return frame_count > 0 ? static_cast<double>(total_decode_time_us) / frame_count : 0.0;
    }

    [[nodiscard]] [[maybe_unused]] double avg_output_drain_time_us() const {
        return frame_count > 0 ? static_cast<double>(total_output_drain_time_us) / frame_count : 0.0;
    }
};

/// Selects the video codec used for decoding.
/// Mirrors encoder_codec on the server side.
/// Controlled by the USE_AV1 preprocessor directive: when defined the default
/// becomes av1; otherwise it remains hevc.
enum class decoder_codec {
#    ifdef USE_AV1
    av1, ///< AV1 (hardware decode, Android 13+ on Quest 3)
    hevc,
    default_codec = av1,
#    else
    hevc, ///< HEVC / H.265 (default)
    av1,
    default_codec = hevc,
#    endif
};

/**
 * @brief Single-eye HEVC or AV1 decoder using Android MediaCodec + AImageReader.
 *
 * Supports both 8-bit (Main profile) and 10-bit (Main 10 profile) bitstreams
 * for HEVC, and 8/10-bit AV1 Main profile.  The codec is selected at
 * construction time via the @p codec parameter (or globally via USE_AV1);
 * the bit depth via @p is_10bit.  Everything else (AImageReader, feeder loop,
 * AHardwareBuffer handoff) is identical between the two paths.
 *
 * AV1 notes:
 *   - MediaCodec MIME type: "video/av01" (IANA registered).
 *   - Requires Android 10+ API for codec availability; hardware decode
 *     (Snapdragon XR2 Gen 2 / Quest 3) is available on Android 13+.
 *   - Profile hint: AV1ProfileMain8 (1) / AV1ProfileMain10HDR10 (4096)
 *     to help the driver allocate the correct pipeline.
 *
 * 10-bit HEVC usage: the motion-vector stream is encoded as HEVC Main 10.
 *   - MediaCodec is told the profile so the Snapdragon XR2 Gen 2 hardware
 *     decoder allocates the correct internal buffers.
 *   - AImageReader still uses AIMAGE_FORMAT_PRIVATE (GPU-opaque), so the
 *     caller never touches the raw 10-bit samples — they stay on the GPU
 *     and are imported into Vulkan as an external AHardwareBuffer.
 *
 * No EGL context or GL state is touched by this class.
 */
class frame_decoder {
public:
    /**
     * @param eye_index  0 = left eye, 1 = right eye (used for logging only).
     * @param width      Frame width after H.265/AV1 alignment padding.
     * @param height     Frame height after H.265/AV1 alignment padding.
     * @param is_10bit   Set to true for 10-bit bitstreams (HEVC Main 10 or AV1 10-bit Main).
     *                   The default (false) selects 8-bit.
     * @param codec      Video codec to decode (default: controlled by USE_AV1 define).
     */
    frame_decoder(int eye_index, int width, int height, bool is_10bit = false,
                  decoder_codec codec = decoder_codec::default_codec);

    ~frame_decoder();

    // Non-copyable
    frame_decoder(const frame_decoder&)            = delete;
    frame_decoder& operator=(const frame_decoder&) = delete;

    /**
     * @brief Initialize AImageReader, MediaCodec, and the feeder thread.
     *
     * Does NOT require a GL context.  Safe to call from any thread.
     *
     * @return true on success.
     */
    bool initialize();

    /**
     * @brief Queue encoded HEVC data for decoding.
     *
     * Thread-safe; can be called from any thread.
     *
     * @param data          Pointer to encoded NAL unit data.
     * @param size          Byte count.
     * @param timestamp_us  PTS in microseconds.
     * @param is_keyframe   True if this is an IDR frame.
     * @param frame_number  Monotonic frame counter for diagnostics.
     * @return true if the data was enqueued successfully.
     */
    bool queue_encoded_data(const uint8_t* data, size_t size, int64_t timestamp_us, bool is_keyframe, uint64_t frame_number);

    /**
     * @brief Acquire the latest decoded AHardwareBuffer and its frame number.
     *
     * Returns the buffer and the server frame_number that was recorded by the
     * drainer at the moment it released this output buffer to the AImageReader
     * surface.  Both values come from the same drainer event so they are
     * guaranteed to be consistent — there is no window where the drainer can
     * update last_decoded_frame_number_ to the next frame between the buffer
     * acquisition and the frame-number read.
     *
     * The returned buffer is retained (AHardwareBuffer_acquire called).
     * The caller MUST call AHardwareBuffer_release() when finished.
     * Returns {nullptr, 0} if no new frame is available.
     *
     * Safe to call from any thread.
     */
    std::pair<AHardwareBuffer*, uint64_t> acquire_latest_buffer();

    /**
     * @brief Check if decoder is initialized and ready.
     *
     * @return true if decoder is ready to decode.
     */
    [[nodiscard]] bool is_ready() const {
        return initialized_.load();
    }

    /**
     * @brief Get the number of frames that have been successfully decoded.
     *
     * This is useful to check if the decoder has actually produced any output.
     *
     * @return Number of frames decoded.
     */
    [[nodiscard]] uint64_t get_frames_decoded() const {
        return frames_decoded_.load();
    }

    /**
     * @brief Check if at least one frame has been released to the AImageReader surface.
     *
     * Returns true as soon as the feeder loop has called
     * AMediaCodec_releaseOutputBuffer(..., render=true) at least once, meaning
     * the codec has produced output and it is available to acquire.  This is
     * intentionally distinct from frames_decoded_, which counts frames that
     * have already been pulled out by the consumer via acquire_latest_buffer().
     * Using frames_decoded_ here created a circular deadlock: the guard
     * prevented acquire_latest_buffer() from ever being called, so
     * frames_decoded_ stayed at zero forever.
     *
     * @return true if the codec has rendered at least one frame to the surface.
     */
    [[nodiscard]] bool has_decoded_frames() const {
        return frames_released_.load() > 0;
    }

    /**
     * @brief Get diagnostic statistics.
     */
    [[nodiscard]] uint64_t get_frames_queued() const {
        return frames_queued_.load();
    }

    [[nodiscard]] uint64_t get_frames_dropped() const {
        return frames_dropped_.load();
    }

    [[nodiscard]] uint64_t get_frames_released() const {
        return frames_released_.load();
    }

    /**
     * @brief Get current input queue depth.
     *
     * @return Number of encoded packets waiting to be decoded.
     */
    [[nodiscard]] size_t get_queue_depth() const;

    /**
     * @brief Get and reset timing statistics.
     *
     * Returns accumulated timing stats and resets the internal counters.
     * Thread-safe.
     *
     * @return decode_timing_stats structure with accumulated timing data.
     */
    decode_timing_stats get_and_reset_timing_stats();

    /**
     * @brief Get timing statistics without resetting.
     *
     * @return Current timing statistics (snapshot).
     */
    [[nodiscard]] decode_timing_stats get_timing_stats() const;

    /**
     * @brief Flush the decoder (clear all pending frames).
     */
    void flush();

    /**
     * @brief Stop the decoder and release resources.
     */
    void stop();

    [[nodiscard]] float get_decode_time() const {
        return last_decode_time_;
    }

    /**
     * @brief Boxcar FPS averaged over the last second.
     *
     * Returns the number of frames decoded in the most recent 1-second window.
     * Thread-safe; may be called from any thread.
     */
    [[nodiscard]] float get_fps() const {
        std::lock_guard<std::mutex> lock(fps_mutex_);
        return static_cast<float>(fps_window_.size());
    }

private:
    struct encoded_packet {
        std::vector<uint8_t>                  data;
        int64_t                               timestamp_us;
        bool                                  is_keyframe;
        std::chrono::steady_clock::time_point queue_time;
        uint64_t                              frame_number; // ← add this
    };

    // Internal helpers
    bool configure_codec();
    void feeder_loop();
    void drainer_loop();

    // Identity
    int eye_index_;
    int width_;
    int height_;

    bool          is_10bit_;    ///< True → 10-bit bitstream (HEVC Main 10 or AV1 10-bit Main); false → 8-bit.
    decoder_codec video_codec_; ///< Video codec selected at construction.

    // ── AImageReader
    AImageReader* image_reader_ = nullptr;

    // Native window for MediaCodec output
    ANativeWindow* native_window_ = nullptr;

    // MediaCodec
    AMediaCodec* codec_ = nullptr;

    // Lifecycle
    std::atomic<bool> running_{false};
    std::atomic<bool> initialized_{false};

    // Input queue (encoded packets waiting to be fed to the codec)
    mutable std::mutex         input_mutex_;
    std::condition_variable    input_cv_;
    std::queue<encoded_packet> input_queue_;

    // Feeder thread: dequeues input buffers from the codec and submits encoded packets.
    // Drainer thread: independently polls dequeueOutputBuffer and releases decoded frames.
    // Separating these prevents the codec's output queue from filling up and stalling
    // input acceptance while the feeder is busy copying the next packet.
    std::thread feeder_thread_;
    std::thread drainer_thread_;

    // Frame counters
    // frames_decoded_  — frames pulled from AImageReader by the consumer.
    // frames_released_ — frames pushed to the AImageReader surface by the drainer
    //                    (i.e. codec output buffers released with render=true).
    //                    has_decoded_frames() checks this, NOT frames_decoded_,
    //                    to avoid the circular deadlock described in has_decoded_frames().
    std::atomic<uint64_t> frames_decoded_{0};
    std::atomic<uint64_t> frames_queued_{0};
    std::atomic<uint64_t> frames_dropped_{0};
    std::atomic<uint64_t> frames_released_{0};

    // Timing
    mutable std::mutex  timing_mutex_;
    decode_timing_stats timing_stats_;

    // Shared between feeder (writes) and drainer (reads+erases).
    // Maps PTS → {queue_time, submit_time, frame_number} where:
    //   queue_time   = when queue_encoded_data() was called (packet entered input_queue_)
    //   submit_time  = when queueInputBuffer() completed (codec accepted the packet)
    //   frame_number = monotonic frame counter from the server, used to look up
    //                  the correct metadata in offload_rendering_client::frame_meta_map_
    // The drainer uses all three fields: timing for stats, frame_number to update
    // last_decoded_frame_number_ so the consumer can retrieve the matching metadata.
    struct timestamp_entry {
        std::chrono::steady_clock::time_point queue_time;
        std::chrono::steady_clock::time_point submit_time;
        uint64_t                              frame_number{0};
    };

    mutable std::mutex                           pending_timestamps_mutex_;
    std::unordered_map<int64_t, timestamp_entry> pending_timestamps_;

    // Written by the drainer with release ordering BEFORE calling
    // releaseOutputBuffer, and read by acquire_latest_buffer() with acquire
    // ordering AFTER AImageReader_acquireLatestImage returns.  This ordering
    // guarantee ensures the frame number always corresponds to an image that
    // is already available in the AImageReader — no mutex required.
    std::atomic<uint64_t> last_decoded_frame_number_{0};

    float last_decode_time_{0.f};

    // AV1: tracks whether the OBU Sequence Header has been submitted to the codec
    // as a CODEC_CONFIG buffer.  Set to true after the first keyframe's sequence
    // header is extracted and submitted; never reset (the codec only needs it once).
    bool codec_config_sent_{false};

    // Boxcar FPS: timestamps of the last N decoded frames within a 1-second
    // sliding window.  Maintained by the drainer thread; access protected by
    // fps_mutex_ since get_fps() may be called from any thread.
    mutable std::mutex                                fps_mutex_;
    std::deque<std::chrono::steady_clock::time_point> fps_window_;
};

} // namespace ILLIXR

#endif // __ANDROID__
