#pragma once

#include "illixr/data_format/openxr_data.hpp"
#include "illixr/data_format/openxr_payloads.h"
#include "illixr/data_format/serialization/openxr_payloads.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"

#include "illixr/audio/miniaudio/miniaudio.h"
#ifdef ILLIXR_DUMP_AUDIO
#include "illixr/audio/audio_dump.hpp"
#endif

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace ILLIXR {

/**
 * @brief Records the client's microphone and sends it to the server, where it becomes the server
 * app's microphone input.
 *
 * miniaudio's audio thread only copies captured frames into a lock-free ring buffer. The
 * threadloop drains the ring in fixed-size chunks and publishes each one as an
 * ixr_event<ixr_audio_chunk> on the network topic "audio_to_server".
 *
 * Environment Variables:
 *   - AUDIO_CAPTURE_DEVICE: part of the capture device's name to match (default: system default device)
 *   - AUDIO_CAPTURE_SAMPLE_RATE: sample rate in Hz (default: 48000)
 *   - AUDIO_CAPTURE_CHANNELS: 1 or 2 (default: 1)
 *   - AUDIO_CAPTURE_CHUNK_MS: chunk length in milliseconds (default: 10)
 */
class audio_capture : public threadloop {
public:
    /**
     * @brief Opens the capture device; it starts recording in start().
     * @param name Plugin name
     * @param pb Phonebook pointer for accessing services
     */
    [[maybe_unused]] audio_capture(const std::string& name, phonebook* pb);
    ~audio_capture() override;

    void start() override;
    void stop() override;

protected:
    /**
     * @brief Waits briefly and skips until a full chunk is queued.
     */
    skip_option _p_should_skip() override;

    /**
     * @brief Publishes every full chunk in the ring.
     */
    void _p_one_iteration() override;

private:
    using audio_event = ixr_event<ixr_audio_chunk>;

    /**
     * @brief miniaudio data callback, runs on the audio thread.
     */
    static void data_callback(ma_device* device, void* output, const void* input, ma_uint32 frame_count);

    bool open_device();
    void capture(const float* capture, ma_uint32 frame_count);
    void read_frames(float* destination, ma_uint32 frame_count);

    const std::shared_ptr<switchboard>       switchboard_;
    switchboard::network_writer<audio_event> writer_;
    const std::string                        device_name_;
    ma_uint32                                sample_rate_;
    ma_uint32                                channels_;
    ma_uint32                                chunk_frames_ = 0;

    ma_context context_{};
    ma_device  device_{};
    ma_pcm_rb  ring_{};
    bool       context_ready_ = false;
    bool       device_ready_  = false;
    bool       ring_ready_    = false;

    // Stream position of the next chunk; frames dropped on the audio thread are skipped, leaving a gap.
    uint64_t              next_frame_       = 0;
    uint64_t              chunks_published_ = 0;
    uint64_t              total_dropped_    = 0;
    std::atomic<uint64_t> dropped_frames_{0};
#ifdef ILLIXR_DUMP_AUDIO
    audio_dump dump_;
#endif
};

} // namespace ILLIXR
