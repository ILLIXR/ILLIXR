#ifdef __ANDROID__
#    include "frame_decoder.hpp"

#    include <cstring>
#    include <iterator>
#    include <media/NdkMediaFormat.h>
#    include <spdlog/spdlog.h>

using namespace ILLIXR;

frame_decoder::frame_decoder(int eye_index, int width, int height, bool is_10bit, decoder_codec codec)
    : eye_index_(eye_index)
    , width_(width)
    , height_(height)
    , is_10bit_(is_10bit)
    , video_codec_(codec) { }

frame_decoder::~frame_decoder() {
    stop();
}

bool frame_decoder::initialize() {
    if (initialized_.load()) {
        spdlog::get("illixr")->warn("[frame_decoder][{}] Already initialized", eye_index_);
        return true;
    }

    // ── Create AImageReader ───────────────────────────────────────────────────
    // AIMAGE_FORMAT_PRIVATE: GPU-only buffer opaque to the CPU.
    // The buffer can be imported directly into Vulkan via
    // vkGetAndroidHardwareBufferPropertiesANDROID without any CPU copies.
    // maxImages=8 gives the codec enough pipeline depth so that a brief
    // consumer stall does not cause releaseOutputBuffer to block and back-
    // pressure the drainer thread.  Each slot is an AHardwareBuffer reference
    // (not a copy of the pixel data), so the memory overhead is negligible.
    media_status_t status = AImageReader_new(width_, height_, AIMAGE_FORMAT_PRIVATE, /*maxImages=*/8, &image_reader_);
    if (status != AMEDIA_OK || image_reader_ == nullptr) {
        spdlog::get("illixr")->error("[frame_decoder][{}] AImageReader_new failed: {}", eye_index_, static_cast<int>(status));
        return false;
    }

    // Retrieve the ANativeWindow that MediaCodec will render into.
    status = AImageReader_getWindow(image_reader_, &native_window_);
    if (status != AMEDIA_OK || native_window_ == nullptr) {
        spdlog::get("illixr")->error("[frame_decoder][{}] AImageReader_getWindow failed: {}", eye_index_,
                                     static_cast<int>(status));
        AImageReader_delete(image_reader_);
        image_reader_ = nullptr;
        return false;
    }
    // Retain the window for the lifetime of the codec.
    ANativeWindow_acquire(native_window_);

    codec_failed_.store(false);

    // IMPORTANT: Set running_ BEFORE configure_codec() because async callbacks
    // can fire immediately after AMediaCodec_start() and they check this flag
    running_.store(true);

    // Configure MediaCodec with async callbacks
    if (!configure_codec()) {
        spdlog::get("illixr")->error("[frame_decoder][{}] Failed to configure codec", eye_index_);
        running_.store(false);
        ANativeWindow_release(native_window_);
        native_window_ = nullptr;
        AImageReader_delete(image_reader_);
        image_reader_ = nullptr;
        return false;
    }

    initialized_.store(true);

    spdlog::get("illixr")->info("[frame_decoder][{}] Initialized {}x{} (AImageReader/Vulkan path)", eye_index_, width_,
                                height_);
    return true;
}

bool frame_decoder::configure_codec() {
#    ifdef USE_AV1
    const bool use_av1 = (video_codec_ == decoder_codec::av1);
#    else
    constexpr bool use_av1 = false;
#    endif // USE_AV1

    const char* mime = use_av1 ? "video/av01" : "video/hevc";

    codec_ = AMediaCodec_createDecoderByType(mime);
    if (!codec_) {
        spdlog::get("illixr")->error("[frame_decoder][{}] Failed to create {} codec", eye_index_, use_av1 ? "AV1" : "HEVC");
        return false;
    }

    AMediaFormat* format = AMediaFormat_new();
    AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, mime);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, width_);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, height_);

#    ifdef USE_AV1
    if (use_av1) {
        if (is_10bit_) {
            // ── 10-bit AV1 Main path ─────────────────────────────────────────
            // AV1ProfileMain10HDR10 = 4096  (android.media.MediaCodecInfo.CodecProfileLevel)
            // Advisory hint — not all drivers act on it, but it costs nothing.
            AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_PROFILE, 4096);

            // Advisory bit-depth hint for drivers that respect it.
            AMediaFormat_setInt32(format, "bit-depth", 10);

            // Max input size: 10-bit AV1 is ~2 bytes/luma sample in 4:2:0.
            AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_MAX_INPUT_SIZE, width_ * height_ * 2);

            // Color metadata: BT.601 full-range, linear transfer.
            // Prevents the runtime from applying HDR tone-mapping to the
            // motion-vector data.
            // Constants from android.media.MediaFormat:
            //   COLOR_TRANSFER_LINEAR  = 1
            //   COLOR_STANDARD_BT601_PAL = 2
            //   COLOR_RANGE_FULL       = 1
            AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_COLOR_TRANSFER, 1);
            AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_COLOR_STANDARD, 2);
            AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_COLOR_RANGE, 1);

            spdlog::get("illixr")->info("[frame_decoder][{}] Configuring AV1 Main 10-bit {}x{}", eye_index_, width_, height_);
        } else {
            // ── 8-bit AV1 Main path ──────────────────────────────────────────
            // AV1ProfileMain8 = 1  (android.media.MediaCodecInfo.CodecProfileLevel)
            AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_PROFILE, 1);
            AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_MAX_INPUT_SIZE, width_ * height_);

            // Match the server's full-range BT.709 conversion and its AV1
            // sequence-header metadata. Explicit MediaFormat hints avoid a
            // vendor default to limited range on the Quest decoder.
            // Android MediaFormat constants:
            //   COLOR_STANDARD_BT709   = 1
            //   COLOR_TRANSFER_SDR_VIDEO = 3
            //   COLOR_RANGE_FULL       = 1
            AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_COLOR_STANDARD, 1);
            AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_COLOR_TRANSFER, 3);
            AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_COLOR_RANGE, 1);

            spdlog::get("illixr")->info("[frame_decoder][{}] Configuring AV1 Main 8-bit {}x{}", eye_index_, width_, height_);
        }
    } else
#    endif // USE_AV1
    {
        if (is_10bit_) {
            // ── 10-bit HEVC Main 10 path ─────────────────────────────────────

            // HEVC Main 10 profile = 2  (HEVCProfileMain10 in CodecProfileLevel)
            AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_PROFILE, 2);

            // Advisory bit-depth hint for drivers that respect it.
            AMediaFormat_setInt32(format, "bit-depth", 10);

            // Max input size: P010 is 2 bytes/luma sample + 1 byte/chroma pair
            // = width * height * 2 bytes for 4:2:0 at 10 bits packed in 16-bit words.
            AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_MAX_INPUT_SIZE, width_ * height_ * 2);

            // Color metadata: BT.601 full-range, linear transfer.
            // This prevents the runtime from treating the motion-vector data as
            // HDR video and applying an unexpected tone-map.
            // Constants from android.media.MediaFormat:
            //   COLOR_TRANSFER_LINEAR  = 1
            //   COLOR_STANDARD_BT601_PAL = 2   (BT.601 — matches server encoder)
            //   COLOR_RANGE_FULL       = 1
            AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_COLOR_TRANSFER, 1);
            AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_COLOR_STANDARD, 2);
            AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_COLOR_RANGE, 1);

            spdlog::get("illixr")->info("[frame_decoder][{}] Configuring HEVC Main 10 (10-bit) {}x{}", eye_index_, width_,
                                        height_);
        } else {
            AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_MAX_INPUT_SIZE, width_ * height_);
            spdlog::get("illixr")->info("[frame_decoder][{}] Configuring HEVC Main (8-bit) {}x{}", eye_index_, width_, height_);
        }
    }

    // ============================================================
    // LOW LATENCY CONFIGURATION - Critical for VR streaming
    // ============================================================

    // Standard Android low-latency key (Android 11+)
    AMediaFormat_setInt32(format, "low-latency", 1);

    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_PRIORITY, 0); // realtime
    AMediaFormat_setInt32(format, "priority", 0);
    AMediaFormat_setInt32(format, "vendor.qti-ext-dec-low-latency.enable", 1);
    AMediaFormat_setInt32(format, "vendor.low-latency.enable", 1);

    // Operating-rate and frame-rate hints: advertise 90 Hz to match the actual
    // input rate so the driver allocates full-rate resources.  Qualcomm drivers
    // on XR2 Gen 2 may use either key depending on the BSP version, so both
    // are set.  Setting these accurately (rather than 72) prevents the codec
    // from being scheduled in a lower-priority tier when actual throughput
    // exceeds the declared rate.
    AMediaFormat_setFloat(format, AMEDIAFORMAT_KEY_OPERATING_RATE, 180.0f);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_FRAME_RATE, 180);
    // Also set max-fps-to-encoder which some Qualcomm drivers respect:
    AMediaFormat_setFloat(format, "max-fps-to-encoder", 180.0f);

    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_LOW_LATENCY, 1);

    // Configure with AImageReader's output window (zero-copy GPU path).
    media_status_t status = AMediaCodec_configure(codec_, format, native_window_, nullptr, 0);
    AMediaFormat_delete(format);

    if (status != AMEDIA_OK) {
        spdlog::get("illixr")->error("[frame_decoder][{}] AMediaCodec_configure failed: {}", eye_index_,
                                     static_cast<int>(status));
        AMediaCodec_delete(codec_);
        codec_ = nullptr;
        return false;
    }

    // Use synchronous dequeue mode.  AMediaCodec async callbacks are not
    // reliable on the Snapdragon XR2 Gen 2 (Quest 3, Android 12): the call
    // returns AMEDIA_OK but on_input_available never fires regardless of
    // whether it is registered before or after configure.  Sync dequeue is
    // simpler and works correctly on this hardware.
    spdlog::get("illixr")->info("[frame_decoder][{}] Using sync dequeue mode", eye_index_);
    status = AMediaCodec_start(codec_);
    if (status != AMEDIA_OK) {
        spdlog::get("illixr")->error("[frame_decoder][{}] AMediaCodec_start failed: {}", eye_index_, static_cast<int>(status));
        AMediaCodec_delete(codec_);
        codec_ = nullptr;
        return false;
    }

    feeder_thread_  = std::thread(&frame_decoder::feeder_loop, this);
    drainer_thread_ = std::thread(&frame_decoder::drainer_loop, this);

    return true;
}

std::pair<AHardwareBuffer*, uint64_t> frame_decoder::acquire_latest_buffer() {
    if (!image_reader_ || !initialized_.load()) {
        return {nullptr, 0};
    }

    AImage*        image  = nullptr;
    media_status_t status = AImageReader_acquireLatestImage(image_reader_, &image);
    if (status != AMEDIA_OK || image == nullptr) {
        return {nullptr, 0};
    }

    AHardwareBuffer* hw_buffer = nullptr;
    status                     = AImage_getHardwareBuffer(image, &hw_buffer);
    if (status != AMEDIA_OK || hw_buffer == nullptr) {
        spdlog::get("illixr")->warn("[frame_decoder][{}] AImage_getHardwareBuffer failed: {}", eye_index_,
                                    static_cast<int>(status));
        AImage_delete(image);
        return {nullptr, 0};
    }

    int64_t image_timestamp_ns = 0;
    status                     = AImage_getTimestamp(image, &image_timestamp_ns);

    // Retain the buffer so the caller can hold it independently of the AImage.
    AHardwareBuffer_acquire(hw_buffer);

    uint64_t frame_num = 0;
    if (status == AMEDIA_OK) {
        std::lock_guard<std::mutex> lock(released_frame_numbers_mutex_);
        const auto                  it = released_frame_numbers_by_timestamp_ns_.find(image_timestamp_ns);
        if (it != released_frame_numbers_by_timestamp_ns_.end()) {
            frame_num = it->second;
            released_frame_numbers_by_timestamp_ns_.erase(released_frame_numbers_by_timestamp_ns_.begin(), std::next(it));
        }
    }

    AImage_delete(image);

    if (frame_num == 0) {
        static std::atomic<uint64_t> missing_timestamp_count{0};
        const uint64_t               count = missing_timestamp_count.fetch_add(1, std::memory_order_relaxed) + 1;
        if (count % 120 == 1) {
            spdlog::get("illixr")->warn("[frame_decoder][{}] No exact frame number for AImage timestamp {} (count={})",
                                        eye_index_, image_timestamp_ns, count);
        }
    }

    uint64_t n = frames_decoded_.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 5 || n % 200 == 0) {
        spdlog::get("illixr")->info("[frame_decoder][{}] acquire_latest_buffer: "
                                    "returning buffer #{} frame_number={} ptr={}",
                                    eye_index_, n, frame_num, static_cast<void*>(hw_buffer));
    }
    return {hw_buffer, frame_num};
}

void frame_decoder::feeder_loop() {
    spdlog::get("illixr")->info("[frame_decoder][{}] Feeder loop started (sync mode)", eye_index_);
    uint64_t packets_fed      = 0;
    uint64_t dequeue_timeouts = 0;

    while (running_.load() && !codec_failed_.load()) {
        // Block until there is an encoded packet to submit.  Do this BEFORE
        // calling dequeueInputBuffer so we never hold a codec buffer slot idle
        // while waiting for the producer.
        encoded_packet pkt;
        {
            std::unique_lock<std::mutex> pkt_lock(input_mutex_);
            input_cv_.wait(pkt_lock, [this] {
                return !input_queue_.empty() || !running_.load();
            });
            if (!running_.load())
                break;
            pkt = std::move(input_queue_.front());
            input_queue_.pop();
        }

#    ifdef USE_AV1
        // AV1: before submitting the very first keyframe, extract the OBU Sequence
        // Header from it and submit it separately as a CODEC_CONFIG buffer.
        // The Qualcomm hardware AV1 decoder on Quest 3 requires this to initialise
        // its internal state; without it P-frames decode as garbage even though
        // the Sequence Header OBU is present inline in the keyframe bitstream.
        //
        // The OBU Sequence Header has obu_type = 1 → header byte = 0x0A (type=1,
        // has_size_field=1, extension_flag=0).  We scan the keyframe for it and
        // submit everything up to and including its end as the CODEC_CONFIG buffer.
        if (video_codec_ == decoder_codec::av1 && pkt.is_keyframe && !codec_config_sent_) {
            // Locate the Sequence Header OBU in the keyframe packet.
            // NVENC low-overhead format: TD OBU (0x12, 0x00) then Sequence Header OBU.
            // Sequence Header OBU header byte: 0x0A (obu_type=1, has_size_field=1).
            const uint8_t* d           = pkt.data.data();
            const size_t   dsz         = pkt.data.size();
            size_t         seq_hdr_end = 0;

            for (size_t i = 0; i + 1 < dsz; i++) {
                uint8_t obu_type = (d[i] >> 3) & 0x0F;
                bool    has_size = (d[i] >> 1) & 0x01;
                size_t  hdr_len  = 1 + (((d[i] >> 2) & 0x01) ? 1 : 0); // +1 if extension_flag

                if (!has_size)
                    break; // cannot determine OBU length without size field

                // Parse LEB128 size
                uint64_t obu_size  = 0;
                size_t   leb_bytes = 0;
                for (size_t j = i + hdr_len; j < dsz && leb_bytes < 8; j++, leb_bytes++) {
                    obu_size |= static_cast<uint64_t>(d[j] & 0x7F) << (7 * leb_bytes);
                    if (!(d[j] & 0x80)) {
                        leb_bytes++;
                        break;
                    }
                }

                size_t obu_end = i + hdr_len + leb_bytes + obu_size;

                if (obu_type == 1) { // OBU_SEQUENCE_HEADER
                    seq_hdr_end = obu_end;
                    break;
                }
                if (obu_type == 6 || obu_type == 3) { // OBU_FRAME or OBU_FRAME_HEADER
                    break;                            // passed headers, stop
                }
                i = obu_end - 1; // advance past this OBU (loop will ++i)
            }

            if (seq_hdr_end > 0 && seq_hdr_end <= dsz) {
                // Submit the bytes up through the end of the Sequence Header OBU
                // as a CODEC_CONFIG buffer (no timestamp needed).
                ssize_t cfg_idx = -1;
                while (running_.load() && !codec_failed_.load() && cfg_idx < 0) {
                    cfg_idx = AMediaCodec_dequeueInputBuffer(codec_, /*timeoutUs=*/2000);
                    if (cfg_idx < 0 && cfg_idx != AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
                        spdlog::get("illixr")->error("[frame_decoder][{}] Fatal dequeueInputBuffer error {} while "
                                                     "submitting AV1 codec configuration",
                                                     eye_index_, cfg_idx);
                        codec_failed_.store(true);
                    }
                }
                if (cfg_idx >= 0) {
                    size_t   cfg_buf_size = 0;
                    uint8_t* cfg_buf      = AMediaCodec_getInputBuffer(codec_, static_cast<size_t>(cfg_idx), &cfg_buf_size);
                    if (cfg_buf && cfg_buf_size >= seq_hdr_end) {
                        std::memcpy(cfg_buf, d, seq_hdr_end);
                        const media_status_t queue_status =
                            AMediaCodec_queueInputBuffer(codec_, static_cast<size_t>(cfg_idx),
                                                         /*offset=*/0, seq_hdr_end,
                                                         /*presentationTimeUs=*/0, AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG);
                        if (queue_status != AMEDIA_OK) {
                            spdlog::get("illixr")->error("[frame_decoder][{}] AV1 CODEC_CONFIG queue failed: {}", eye_index_,
                                                         static_cast<int>(queue_status));
                            codec_failed_.store(true);
                            break;
                        }
                        codec_config_sent_ = true;
                        spdlog::get("illixr")->info("[frame_decoder][{}] AV1 CODEC_CONFIG submitted ({} bytes)", eye_index_,
                                                    seq_hdr_end);
                    }
                }
            } else {
                spdlog::get("illixr")->warn("[frame_decoder][{}] AV1: could not locate Sequence Header OBU "
                                            "in keyframe (size={}), skipping CODEC_CONFIG",
                                            eye_index_, dsz);
                codec_config_sent_ = true; // don't retry every keyframe
            }
        }
#    endif // USE_AV1

        if (codec_failed_.load()) {
            break;
        }

        // Acquire a codec input buffer.  Use a short timeout so the thread
        // stays responsive to shutdown without busy-spinning.  Output draining
        // is handled by the dedicated drainer thread, so a timeout here does
        // NOT stall frame delivery.
        ssize_t buf_idx          = -1;
        int     dequeue_attempts = 0;
        while (running_.load() && !codec_failed_.load() && buf_idx < 0) {
            buf_idx = AMediaCodec_dequeueInputBuffer(codec_, /*timeoutUs=*/2000);
            if (buf_idx == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
                dequeue_attempts++;
                dequeue_timeouts++;
                if (dequeue_attempts == 1 || dequeue_attempts % 10 == 0) {
                    spdlog::get("illixr")->warn("[frame_decoder][{}] dequeueInputBuffer returned {} "
                                                "(attempt {}), packets_fed={}",
                                                eye_index_, buf_idx, dequeue_attempts, packets_fed);
                }
            } else if (buf_idx < 0) {
                spdlog::get("illixr")->error("[frame_decoder][{}] Fatal dequeueInputBuffer error {} after {} packets",
                                             eye_index_, buf_idx, packets_fed);
                codec_failed_.store(true);
            }
        }
        if (!running_.load() || codec_failed_.load())
            break;

        // Write the encoded data directly into the codec's input buffer to
        // avoid a redundant copy.  The packet's data vector was already
        // allocated by queue_encoded_data; this memcpy is the only one on
        // the hot path.
        size_t   buf_size = 0;
        uint8_t* buf      = AMediaCodec_getInputBuffer(codec_, static_cast<size_t>(buf_idx), &buf_size);
        if (buf && buf_size >= pkt.data.size()) {
            std::memcpy(buf, pkt.data.data(), pkt.data.size());
            uint32_t flags = pkt.is_keyframe ? AMEDIACODEC_BUFFER_FLAG_KEY_FRAME : 0;

            // Install the PTS mapping before exposing the input buffer to
            // MediaCodec. A low-latency decoder may make its output visible as
            // soon as queueInputBuffer returns; publishing the mapping first
            // prevents the drainer from observing an otherwise valid output
            // without its exact frame number.
            const auto submit_time = std::chrono::steady_clock::now();
            {
                std::lock_guard<std::mutex> ts_lock(pending_timestamps_mutex_);
                pending_timestamps_[pkt.timestamp_us] = {pkt.queue_time, submit_time, pkt.frame_number};
            }
            const media_status_t queue_status =
                AMediaCodec_queueInputBuffer(codec_, static_cast<size_t>(buf_idx),
                                             /*offset=*/0, pkt.data.size(), static_cast<uint64_t>(pkt.timestamp_us), flags);
            if (queue_status != AMEDIA_OK) {
                {
                    std::lock_guard<std::mutex> ts_lock(pending_timestamps_mutex_);
                    pending_timestamps_.erase(pkt.timestamp_us);
                }
                spdlog::get("illixr")->error("[frame_decoder][{}] queueInputBuffer failed for frame {} ({} bytes, "
                                             "keyframe={}): {}",
                                             eye_index_, pkt.frame_number, pkt.data.size(), pkt.is_keyframe,
                                             static_cast<int>(queue_status));
                codec_failed_.store(true);
                break;
            }
            packets_fed++;

            const auto now = std::chrono::steady_clock::now();
            uint64_t   queue_us =
                static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(now - pkt.queue_time).count());

            {
                std::lock_guard<std::mutex> t_lock(timing_mutex_);
                timing_stats_.total_queue_time_us += queue_us;
            }
        } else {
            spdlog::get("illixr")->warn("[frame_decoder][{}] Input buffer too small "
                                        "({} < {}), dropping packet",
                                        eye_index_, buf_size, pkt.data.size());
        }
    }

    spdlog::get("illixr")->info("[frame_decoder][{}] Feeder thread exiting: "
                                "packets_fed={} dequeue_timeouts={}",
                                eye_index_, packets_fed, dequeue_timeouts);
}

void frame_decoder::drainer_loop() {
    spdlog::get("illixr")->info("[frame_decoder][{}] Drainer loop started", eye_index_);
    uint64_t output_drained        = 0;
    uint64_t output_format_changes = 0;

    while (running_.load() && !codec_failed_.load()) {
        AMediaCodecBufferInfo info{};
        // Use a 1ms blocking timeout: responsive enough to log per-frame timing
        // accurately without busy-spinning.  At 90 Hz one frame is ~11ms, so
        // 1ms gives ≤1ms measurement jitter on decode latency.
        ssize_t out_idx = AMediaCodec_dequeueOutputBuffer(codec_, &info, /*timeoutUs=*/1000);

        if (out_idx >= 0) {
            // Decode latency: submit_time → output available.
            // Total latency:  queue_time  → output available.
            auto     drain_start = std::chrono::steady_clock::now();
            uint64_t decode_us   = 0; // submit → output
            uint64_t total_us    = 0; // queue_encoded_data → output
            // A missing timestamp entry must remain unmatched. Falling back to
            // the previous output number can pair a valid image with another
            // frame's pose/FOV and causes visible world-locked jitter.
            uint64_t decoded_frame_number = 0;
            {
                std::lock_guard<std::mutex> ts_lock(pending_timestamps_mutex_);
                auto                        it = pending_timestamps_.find(info.presentationTimeUs);
                if (it != pending_timestamps_.end()) {
                    decode_us = static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::microseconds>(drain_start - it->second.submit_time).count());
                    total_us = static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::microseconds>(drain_start - it->second.queue_time).count());
                    decoded_frame_number = it->second.frame_number;
                    pending_timestamps_.erase(it);
                }
            }

            // Log per-frame decode and total latency for every frame.
            // decode_us: time the codec hardware took (submit → output ready).
            // total_us:  end-to-end latency (queue_encoded_data → output ready),
            //            includes any time the packet spent waiting in input_queue_.
            // fps:       boxcar average over the last 1 second.
            float current_fps = 0.0f;
            {
                std::lock_guard<std::mutex> fps_lock(fps_mutex_);
                fps_window_.push_back(drain_start);
                // Evict timestamps older than 1 second from the front.
                const auto cutoff = drain_start - std::chrono::seconds(1);
                while (!fps_window_.empty() && fps_window_.front() < cutoff) {
                    fps_window_.pop_front();
                }
                current_fps = static_cast<float>(fps_window_.size());
            }

            if (decode_us > 0) {
                spdlog::get("illixr")->info("[frame_decoder][{}] frame #{}: "
                                            "decode={:.2f}ms  total={:.2f}ms  fps={:.1f}",
                                            eye_index_, output_drained + 1, static_cast<double>(decode_us) / 1000.0,
                                            static_cast<double>(total_us) / 1000.0, current_fps);
            }

            // Record the exact timestamp → frame mapping before exposing this
            // output to AImageReader. AImage_getTimestamp returns the MediaCodec
            // presentation timestamp in nanoseconds, so the consumer can recover
            // this precise frame number even if newer outputs are released while
            // it is acquiring the image.
            if (decoded_frame_number != 0) {
                const int64_t               timestamp_ns = info.presentationTimeUs * 1'000LL;
                std::lock_guard<std::mutex> released_lock(released_frame_numbers_mutex_);
                released_frame_numbers_by_timestamp_ns_[timestamp_ns] = decoded_frame_number;
                while (released_frame_numbers_by_timestamp_ns_.size() > 64) {
                    released_frame_numbers_by_timestamp_ns_.erase(released_frame_numbers_by_timestamp_ns_.begin());
                }
            }
            last_decoded_frame_number_.store(decoded_frame_number, std::memory_order_release);
            AMediaCodec_releaseOutputBuffer(codec_, static_cast<size_t>(out_idx), /*render=*/true);
            auto drain_end = std::chrono::steady_clock::now();

            output_drained++;

            uint64_t drain_us =
                static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(drain_end - drain_start).count());

            {
                std::lock_guard<std::mutex> t_lock(timing_mutex_);
                timing_stats_.total_output_drain_time_us += drain_us;
                if (decode_us > 0) {
                    timing_stats_.total_decode_time_us += decode_us;
                    if (decode_us < timing_stats_.min_decode_latency_us) {
                        timing_stats_.min_decode_latency_us = decode_us;
                    }
                    if (decode_us > timing_stats_.max_decode_latency_us) {
                        timing_stats_.max_decode_latency_us = decode_us;
                    }
                    timing_stats_.frame_count++;
                }
            }

            // Signal has_decoded_frames() as soon as the first frame is
            // available on the AImageReader surface.
            frames_released_.fetch_add(1, std::memory_order_relaxed);
        } else if (out_idx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            output_format_changes++;
            AMediaFormat* fmt     = AMediaCodec_getOutputFormat(codec_);
            const char*   fmt_str = fmt ? AMediaFormat_toString(fmt) : "null";
            spdlog::get("illixr")->info("[frame_decoder][{}] Output format changed (#{}: {})", eye_index_,
                                        output_format_changes, fmt_str);
            if (fmt)
                AMediaFormat_delete(fmt);
        } else if (out_idx != AMEDIACODEC_INFO_TRY_AGAIN_LATER && out_idx != AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
            spdlog::get("illixr")->error("[frame_decoder][{}] Fatal dequeueOutputBuffer error {} after {} outputs", eye_index_,
                                         out_idx, output_drained);
            codec_failed_.store(true);
            input_cv_.notify_all();
            break;
        }
        // AMEDIACODEC_INFO_TRY_AGAIN_LATER (-1) and other negative values are
        // normal — they just mean no output was ready within the timeout.
    }

    spdlog::get("illixr")->info("[frame_decoder][{}] Drainer thread exiting: "
                                "output_drained={}",
                                eye_index_, output_drained);
}

bool frame_decoder::queue_encoded_data(const uint8_t* data, size_t size, int64_t timestamp_us, bool is_keyframe,
                                       uint64_t frame_number) {
    if (!running_.load() || !initialized_.load() || codec_failed_.load()) {
        return false;
    }

    encoded_packet pkt;
    pkt.timestamp_us = timestamp_us;
    pkt.is_keyframe  = is_keyframe;
    pkt.queue_time   = std::chrono::steady_clock::now();
    pkt.frame_number = frame_number;

#    ifdef USE_AV1
    if (video_codec_ == decoder_codec::av1 && !is_keyframe) {
        // The Qualcomm XR2 Gen 2 AV1 hardware decoder requires every temporal
        // unit to begin with a Temporal Delimiter OBU (TD OBU).  NVENC only
        // emits the TD on keyframes; P-frames arrive without one.  Without the
        // TD the decoder treats each P-frame as a continuation of the previous
        // temporal unit, misparsing frame boundaries and producing garbage output.
        //
        // TD OBU format (low-overhead bitstream, AV1 spec §5.2):
        //   byte 0: OBU header — obu_type=2 (TD), extension_flag=0,
        //           has_size_field=1, reserved=0  →  0x12
        //   byte 1: obu_size (LEB128) = 0, because the TD payload is empty  →  0x00
        static constexpr uint8_t kTD[2] = {0x12, 0x00};
        pkt.data.reserve(sizeof(kTD) + size);
        pkt.data.insert(pkt.data.end(), kTD, kTD + sizeof(kTD));
        pkt.data.insert(pkt.data.end(), data, data + size);
    } else {
        pkt.data = std::vector<uint8_t>(data, data + size);
    }
#    else
    pkt.data = std::vector<uint8_t>(data, data + size);
#    endif // USE_AV1

    {
        std::lock_guard<std::mutex> lock(input_mutex_);

        // Do not drop frames here.  The receiver loop in offload_rendering_client
        // already enforces MAX_DECODER_QUEUE_DEPTH and drops whole frames atomically
        // before they reach this point.  Dropping oldest frames blindly here is
        // dangerous for AV1: any dropped P-frame breaks the reference chain and
        // causes decoder corruption until the next keyframe.
        //
        // Safety cap: if the queue somehow grows beyond 16 (double the receiver-side
        // limit), drop the oldest entry only if it is NOT a keyframe.  Keyframes are
        // never dropped because they reset the reference frame state; losing one
        // would cause corruption until the next keyframe arrives.
        constexpr size_t kMaxQueueDepth = 16;
        if (input_queue_.size() >= kMaxQueueDepth && !input_queue_.front().is_keyframe) {
            spdlog::get("illixr")->warn("[frame_decoder][{}] queue overflow ({} >= {}), dropping oldest P-frame", eye_index_,
                                        input_queue_.size(), kMaxQueueDepth);
            input_queue_.pop();
            frames_dropped_.fetch_add(1, std::memory_order_relaxed);
        }

        input_queue_.push(std::move(pkt));
        frames_queued_.fetch_add(1, std::memory_order_relaxed);
    }

    input_cv_.notify_one();

    return true;
}

size_t frame_decoder::get_queue_depth() const {
    std::lock_guard<std::mutex> lock(input_mutex_);
    return input_queue_.size();
}

decode_timing_stats frame_decoder::get_and_reset_timing_stats() {
    std::lock_guard<std::mutex> lock(timing_mutex_);
    decode_timing_stats         result = timing_stats_;
    timing_stats_.reset();
    return result;
}

decode_timing_stats frame_decoder::get_timing_stats() const {
    std::lock_guard<std::mutex> lock(timing_mutex_);
    return timing_stats_;
}

void frame_decoder::flush() {
    if (!codec_) {
        return;
    }

    AMediaCodec_flush(codec_);
    {
        std::lock_guard<std::mutex> lock(input_mutex_);
        while (!input_queue_.empty()) {
            input_queue_.pop();
        }
    }
    // Discard any pending timestamp entries — their output buffers will never
    // arrive after a flush, so they would otherwise accumulate indefinitely.
    {
        std::lock_guard<std::mutex> ts_lock(pending_timestamps_mutex_);
        pending_timestamps_.clear();
    }
    {
        std::lock_guard<std::mutex> released_lock(released_frame_numbers_mutex_);
        released_frame_numbers_by_timestamp_ns_.clear();
    }
    spdlog::get("illixr")->debug("[frame_decoder][{}] Flushed", eye_index_);
}

void frame_decoder::stop() {
    if (!running_.exchange(false)) {
        return; // already stopped
    }

    // Wake and join feeder thread.
    {
        std::lock_guard<std::mutex> lock(input_mutex_);
        input_cv_.notify_all();
    }

    if (feeder_thread_.joinable()) {
        spdlog::get("illixr")->debug("[frame_decoder][{}] Waiting for feeder thread to exit", eye_index_);
        feeder_thread_.join();
    }

    if (drainer_thread_.joinable()) {
        spdlog::get("illixr")->debug("[frame_decoder][{}] Waiting for drainer thread to exit", eye_index_);
        drainer_thread_.join();
    }

    {
        std::lock_guard<std::mutex> released_lock(released_frame_numbers_mutex_);
        released_frame_numbers_by_timestamp_ns_.clear();
    }

    if (codec_) {
        AMediaCodec_stop(codec_);
        AMediaCodec_delete(codec_);
        codec_ = nullptr;
    }

    if (native_window_) {
        ANativeWindow_release(native_window_);
        native_window_ = nullptr;
    }

    if (image_reader_) {
        AImageReader_delete(image_reader_);
        image_reader_ = nullptr;
    }

    initialized_.store(false);
    spdlog::get("illixr")->info("[frame_decoder][{}] Stopped", eye_index_);
}

#endif // __ANDROID__
