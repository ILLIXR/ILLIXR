#include "plugin.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <spdlog/spdlog.h>
#include <thread>

using namespace ILLIXR;

// Default configuration values
static constexpr int DEFAULT_SAMPLE_RATE = 48000;
static constexpr int DEFAULT_CHANNELS    = 1;
static constexpr int DEFAULT_CHUNK_MS    = 10;
static constexpr int MIN_SAMPLE_RATE     = 8000;
static constexpr int MAX_SAMPLE_RATE     = 192000;

// How long the threadloop waits before checking the ring again when less than a chunk is queued
static constexpr std::chrono::milliseconds IDLE_WAIT{2};

/**
 * @brief Finds the first capture device whose name contains name_part.
 */
static bool find_capture_device(ma_context& context, const std::string& name_part, ma_device_id& id) {
    ma_device_info* infos = nullptr;
    ma_uint32       count = 0;
    if (ma_context_get_devices(&context, nullptr, nullptr, &infos, &count) != MA_SUCCESS) {
        return false;
    }

    for (ma_uint32 i = 0; i < count; ++i) {
        if (std::string{infos[i].name}.find(name_part) != std::string::npos) {
            id = infos[i].id;
            return true;
        }
    }

    spdlog::get("illixr")->warn("[audio_capture] No capture device matches '{}', available devices:", name_part);
    for (ma_uint32 i = 0; i < count; ++i) {
        spdlog::get("illixr")->warn("[audio_capture]   {}", infos[i].name);
    }
    return false;
}

[[maybe_unused]] audio_capture::audio_capture(const std::string& name, phonebook* pb)
    : threadloop{name, pb}
    , switchboard_{pb->lookup_impl<switchboard>()}
    , writer_{switchboard_->get_network_writer<audio_event>("audio_from_client")}
    , device_name_{switchboard_->get_env("AUDIO_CAPTURE_DEVICE", "")}
    , sample_rate_{static_cast<ma_uint32>(
          std::clamp(switchboard_->get_env_int("AUDIO_CAPTURE_SAMPLE_RATE", DEFAULT_SAMPLE_RATE), MIN_SAMPLE_RATE, MAX_SAMPLE_RATE))}
    , channels_{static_cast<ma_uint32>(
          std::clamp(switchboard_->get_env_int("AUDIO_CAPTURE_CHANNELS", DEFAULT_CHANNELS), 1, IXR_AUDIO_MAX_CHANNELS))}
#ifdef ILLIXR_DUMP_AUDIO
    , dump_{"/sdcard/Android/data/com.example.native_activity/", "audio_capture"}
#endif
{
    const int chunk_ms = std::max(switchboard_->get_env_int("AUDIO_CAPTURE_CHUNK_MS", DEFAULT_CHUNK_MS), 1);
    chunk_frames_      = std::clamp<ma_uint32>(sample_rate_ * chunk_ms / 1000, 1, IXR_AUDIO_MAX_FRAMES);

    device_ready_ = open_device();
}

audio_capture::~audio_capture() {
    if (device_ready_) {
        ma_device_uninit(&device_);
    }
    if (ring_ready_) {
        ma_pcm_rb_uninit(&ring_);
    }
    if (context_ready_) {
        ma_context_uninit(&context_);
    }
}

void audio_capture::start() {
    threadloop::start();

    if (device_ready_ && ma_device_start(&device_) != MA_SUCCESS) {
        spdlog::get("illixr")->error("[audio_capture] Failed to start the capture device");
    }
}

void audio_capture::stop() {
    if (device_ready_) {
        ma_device_stop(&device_);
    }

    threadloop::stop();

    spdlog::get("illixr")->info("[audio_capture] Published {} chunks, dropped {} frames", chunks_published_,
                                total_dropped_ + dropped_frames_.load(std::memory_order_relaxed));
}

threadloop::skip_option audio_capture::_p_should_skip() {
    if (device_ready_ && ma_pcm_rb_available_read(&ring_) >= chunk_frames_) {
        return skip_option::run;
    }

    std::this_thread::sleep_for(IDLE_WAIT);
    return skip_option::skip_and_yield;
}

void audio_capture::_p_one_iteration() {
    while (ma_pcm_rb_available_read(&ring_) >= chunk_frames_) {
        // Frames the audio thread could not queue are a gap in the stream, so their indices are skipped.
        const uint64_t dropped = dropped_frames_.exchange(0, std::memory_order_relaxed);
        next_frame_ += dropped;
        total_dropped_ += dropped;

        ixr_audio_chunk chunk{};
        chunk.start_frame   = next_frame_;
        chunk.sample_rate   = sample_rate_;
        chunk.channel_count = channels_;
        chunk.frame_count   = chunk_frames_;
        read_frames(chunk.samples, chunk_frames_);
        next_frame_ += chunk_frames_;
#ifdef ILLIXR_DUMP_AUDIO
        dump_.write(chunk.samples, chunk.frame_count, chunk.channel_count, chunk.sample_rate,
                    chunk.start_frame);
#endif
        writer_.put(writer_.allocate(chunk, now_monotonic_ns()));
        ++chunks_published_;
    }
}

bool audio_capture::open_device() {
    if (ma_context_init(nullptr, 0, nullptr, &context_) != MA_SUCCESS) {
        spdlog::get("illixr")->error("[audio_capture] Failed to initialize the audio context");
        return false;
    }
    context_ready_ = true;

    ma_device_id device_id{};
    const bool   have_id = !device_name_.empty() && find_capture_device(context_, device_name_, device_id);

    // miniaudio converts from the device's native format, so the stream always has the configured format.
    ma_device_config config   = ma_device_config_init(ma_device_type_capture);
    config.capture.pDeviceID  = have_id ? &device_id : nullptr;
    config.capture.format     = ma_format_f32;
    config.capture.channels   = channels_;
    config.sampleRate         = sample_rate_;
    config.periodSizeInFrames = chunk_frames_;
    config.dataCallback       = data_callback;
    config.pUserData          = this;
    // Android: the voice preset enables the platform's echo canceller and noise suppressor where available.
    config.aaudio.inputPreset = ma_aaudio_input_preset_voice_communication;

    if (ma_device_init(&context_, &config, &device_) != MA_SUCCESS) {
        spdlog::get("illixr")->error("[audio_capture] Failed to open the capture device");
        return false;
    }

    // One second of headroom between the audio thread and the threadloop.
    if (ma_pcm_rb_init(ma_format_f32, channels_, sample_rate_, nullptr, nullptr, &ring_) != MA_SUCCESS) {
        spdlog::get("illixr")->error("[audio_capture] Failed to allocate the ring buffer");
        ma_device_uninit(&device_);
        return false;
    }
    ring_ready_ = true;

    spdlog::get("illixr")->info("[audio_capture] Capturing '{}' at {} Hz, {} channel(s), {} frames per chunk",
                                device_.capture.name, sample_rate_, channels_, chunk_frames_);
    return true;
}

void audio_capture::data_callback(ma_device* device, void* output, const void* input, ma_uint32 frame_count) {
    (void) output;
    static_cast<audio_capture*>(device->pUserData)->capture(static_cast<const float*>(input), frame_count);
}

void audio_capture::capture(const float* capture, ma_uint32 frame_count) {
    ma_uint32 written = 0;
    while (written < frame_count) {
        ma_uint32 frames = frame_count - written;
        void*     buffer = nullptr;
        if (ma_pcm_rb_acquire_write(&ring_, &frames, &buffer) != MA_SUCCESS || frames == 0) {
            break;
        }

        std::memcpy(buffer, capture + static_cast<size_t>(written) * channels_, static_cast<size_t>(frames) * channels_ * sizeof(float));
        ma_pcm_rb_commit_write(&ring_, frames);
        written += frames;
    }

    if (written < frame_count) {
        dropped_frames_.fetch_add(frame_count - written, std::memory_order_relaxed);
    }
}

void audio_capture::read_frames(float* destination, ma_uint32 frame_count) {
    ma_uint32 read = 0;
    while (read < frame_count) {
        ma_uint32 frames = frame_count - read;
        void*     buffer = nullptr;
        if (ma_pcm_rb_acquire_read(&ring_, &frames, &buffer) != MA_SUCCESS || frames == 0) {
            break;
        }

        std::memcpy(destination + static_cast<size_t>(read) * channels_, buffer, static_cast<size_t>(frames) * channels_ * sizeof(float));
        ma_pcm_rb_commit_read(&ring_, frames);
        read += frames;
    }
}

PLUGIN_MAIN(audio_capture)
