#pragma once

#include "illixr/data_format/openxr_data.hpp"
#include "illixr/data_format/openxr_payloads.h"
#include "illixr/data_format/serialization/openxr_payloads.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/plugin.hpp"
#include "illixr/switchboard.hpp"

#include "illixr/audio/miniaudio/miniaudio.h"
#ifdef ILLIXR_DUMP_AUDIO
#include "illixr/audio/audio_dump.hpp"
#endif

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace ILLIXR {

/**
 * @brief Plays the captured audio output, received from the server, on the client's
 * speakers or headphones.
 *
 * Chunks arrive on the network topic "audio_to_client" and are queued in a lock-free ring
 * buffer that miniaudio's audio thread plays from. The playback device is opened when the first
 * chunk arrives, at the stream's sample rate and channel count, and reopened if they change;
 * miniaudio converts to the hardware's format. Playback waits for a prefill before starting and
 * again after an underrun, and skips ahead when the queue grows past the latency limit, which
 * absorbs clock drift between server and client.
 *
 * Environment Variables:
 *   - AUDIO_INPUT_DEVICE: part of the playback device's name to match (default: system default device)
 *   - AUDIO_INPUT_PREFILL_MS: audio queued before playback starts (default: 40)
 *   - AUDIO_INPUT_MAX_LATENCY_MS: queued audio above which playback skips ahead (default: 150)
 */
class audio_input : public plugin {
public:
    /**
     * @brief Construct the plugin; nothing plays until the first chunk arrives after start().
     * @param name Plugin name
     * @param pb Phonebook pointer for accessing services
     */
    [[maybe_unused]] audio_input(const std::string& name, phonebook* pb);
    ~audio_input() override;

    void start() override;
    void stop() override;

private:
    using audio_event = ixr_event<ixr_audio_chunk>;

    /**
     * @brief miniaudio data callback, runs on the audio thread.
     */
    static void data_callback(ma_device* device, void* output, const void* input, ma_uint32 frame_count);

    void receive(const switchboard::ptr<const audio_event>& event);
    bool reopen_device(ma_uint32 sample_rate, ma_uint32 channels);
    bool open_device(ma_uint32 sample_rate, ma_uint32 channels);
    void close_device();
    bool write_frames(const float* source, ma_uint32 frame_count);
    void playback(float* output, ma_uint32 frame_count);
    void log_stats();

    const std::shared_ptr<switchboard> switchboard_;
    const std::string                  device_name_;
    const int                          prefill_ms_;
    const int                          max_latency_ms_;

    // Guards opening and closing the device and the receive path; the audio thread never takes it.
    std::mutex device_mutex_;
    bool       running_ = false;

    ma_context context_{};
    ma_device  device_{};
    ma_pcm_rb  ring_{};
    bool       context_ready_ = false;
    bool       device_ready_  = false;
    ma_uint32  sample_rate_   = 0;
    ma_uint32  channels_      = 0;
    ma_uint32  prefill_frames_     = 0;
    ma_uint32  max_queued_frames_  = 0;
    ma_uint32  failed_sample_rate_ = 0;
    ma_uint32  failed_channels_    = 0;

    // Audio thread only
    bool buffering_ = true;

    // Stream checks, receive path only
    uint64_t expected_frame_      = 0;
    bool     have_expected_frame_ = false;

    // Statistics
    uint64_t                              chunks_received_ = 0;
    uint64_t                              gap_frames_      = 0;
    uint64_t                              invalid_chunks_  = 0;
    uint64_t                              ring_overflows_  = 0;
    std::atomic<uint64_t>                 underruns_{0};
    std::atomic<uint64_t>                 latency_trims_{0};
    uint64_t                              logged_problems_ = 0;
    std::chrono::steady_clock::time_point next_stats_time_;
#ifdef ILLIXR_DUMP_AUDIO
    audio_dump dump_;
#endif
};

} // namespace ILLIXR
