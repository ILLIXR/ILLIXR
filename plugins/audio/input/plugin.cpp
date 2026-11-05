#include "plugin.hpp"

#include <algorithm>
#include <cstring>
#include <spdlog/spdlog.h>

using namespace ILLIXR;

// Default configuration values
static constexpr int DEFAULT_PREFILL_MS     = 40;
static constexpr int DEFAULT_MAX_LATENCY_MS = 150;
static constexpr int MIN_SAMPLE_RATE        = 8000;
static constexpr int MAX_SAMPLE_RATE        = 192000;

// The ring holds one second, so the latency limit has to stay below that.
static constexpr int MAX_LATENCY_LIMIT_MS = 900;

// Problems (gaps, underruns, trims, overflows) are logged at most this often.
static constexpr std::chrono::seconds STATS_INTERVAL{5};

/**
 * @brief Finds the first playback device whose name contains name_part.
 */
static bool find_playback_device(ma_context& context, const std::string& name_part, ma_device_id& id) {
    ma_device_info* infos = nullptr;
    ma_uint32       count = 0;
    if (ma_context_get_devices(&context, &infos, &count, nullptr, nullptr) != MA_SUCCESS) {
        return false;
    }

    for (ma_uint32 i = 0; i < count; ++i) {
        if (std::string{infos[i].name}.find(name_part) != std::string::npos) {
            id = infos[i].id;
            return true;
        }
    }

    spdlog::get("illixr")->warn("[audio_input] No playback device matches '{}', available devices:", name_part);
    for (ma_uint32 i = 0; i < count; ++i) {
        spdlog::get("illixr")->warn("[audio_input]   {}", infos[i].name);
    }
    return false;
}

[[maybe_unused]] audio_input::audio_input(const std::string& name, phonebook* pb)
    : plugin{name, pb}
    , switchboard_{pb->lookup_impl<switchboard>()}
    , device_name_{switchboard_->get_env("AUDIO_INPUT_DEVICE", "")}
    , prefill_ms_{std::clamp(switchboard_->get_env_int("AUDIO_INPUT_PREFILL_MS", DEFAULT_PREFILL_MS), 0, MAX_LATENCY_LIMIT_MS)}
    , max_latency_ms_{std::clamp(switchboard_->get_env_int("AUDIO_INPUT_MAX_LATENCY_MS", DEFAULT_MAX_LATENCY_MS), 10,
                                 MAX_LATENCY_LIMIT_MS)}
#ifdef ILLIXR_DUMP_AUDIO
    , dump_{"/sdcard/Android/data/com.example.native_activity/", "audio_input"}
#endif
{
    context_ready_ = ma_context_init(nullptr, 0, nullptr, &context_) == MA_SUCCESS;
    if (!context_ready_) {
        spdlog::get("illixr")->error("[audio_input] Failed to initialize the audio context");
    }
}

audio_input::~audio_input() {
    {
        std::lock_guard<std::mutex> lock{device_mutex_};
        running_ = false;
        close_device();
    }

    if (context_ready_) {
        ma_context_uninit(&context_);
    }
}

void audio_input::start() {
    {
        std::lock_guard<std::mutex> lock{device_mutex_};
        running_         = true;
        next_stats_time_ = std::chrono::steady_clock::now() + STATS_INTERVAL;
    }

    switchboard_->schedule<audio_event>(id_, "audio_to_client",
                                        [this](const switchboard::ptr<const audio_event>& event, std::size_t) {
                                            receive(event);
                                        });
    plugin::start();
}

void audio_input::stop() {
    {
        std::lock_guard<std::mutex> lock{device_mutex_};
        running_ = false;
        close_device();
        spdlog::get("illixr")->info("[audio_input] Received {} chunks, gap frames {}, underruns {}, latency trims {}, "
                                    "ring overflows {}, invalid chunks {}",
                                    chunks_received_, gap_frames_, underruns_.load(), latency_trims_.load(), ring_overflows_,
                                    invalid_chunks_);
    }

    plugin::stop();
}

void audio_input::receive(const switchboard::ptr<const audio_event>& event) {
    const ixr_audio_chunk& chunk = event->data;

    std::lock_guard<std::mutex> lock{device_mutex_};
    if (!running_ || !context_ready_) {
        return;
    }

    if (chunk.frame_count == 0 || chunk.frame_count > IXR_AUDIO_MAX_FRAMES || chunk.channel_count == 0 ||
        chunk.channel_count > IXR_AUDIO_MAX_CHANNELS || chunk.sample_rate < static_cast<uint32_t>(MIN_SAMPLE_RATE) ||
        chunk.sample_rate > static_cast<uint32_t>(MAX_SAMPLE_RATE)) {
        ++invalid_chunks_;
        return;
    }

    if (!device_ready_ || chunk.sample_rate != sample_rate_ || chunk.channel_count != channels_) {
        if (!reopen_device(chunk.sample_rate, chunk.channel_count)) {
            return;
        }
    }

    ++chunks_received_;

    // A jump forward means chunks were lost upstream; a jump back is a restarted stream.
    if (have_expected_frame_ && chunk.start_frame > expected_frame_) {
        gap_frames_ += chunk.start_frame - expected_frame_;
    }
    expected_frame_      = chunk.start_frame + chunk.frame_count;
    have_expected_frame_ = true;

#ifdef ILLIXR_DUMP_AUDIO
    dump_.write(chunk.samples, chunk.frame_count, chunk.channel_count, chunk.sample_rate, chunk.start_frame);
#endif
    if (!write_frames(chunk.samples, chunk.frame_count)) {
        ++ring_overflows_;
    }

    log_stats();
}

bool audio_input::reopen_device(ma_uint32 sample_rate, ma_uint32 channels) {
    // Don't retry a format that already failed for every chunk that follows.
    if (sample_rate == failed_sample_rate_ && channels == failed_channels_) {
        return false;
    }

    close_device();
    if (open_device(sample_rate, channels)) {
        failed_sample_rate_ = 0;
        failed_channels_    = 0;
        return true;
    }

    failed_sample_rate_ = sample_rate;
    failed_channels_    = channels;
    return false;
}

bool audio_input::open_device(ma_uint32 sample_rate, ma_uint32 channels) {
    // One second of queue, the latency limit keeps it from filling.
    if (ma_pcm_rb_init(ma_format_f32, channels, sample_rate, nullptr, nullptr, &ring_) != MA_SUCCESS) {
        spdlog::get("illixr")->error("[audio_input] Failed to allocate the ring buffer");
        return false;
    }

    ma_device_id device_id{};
    const bool   have_id = !device_name_.empty() && find_playback_device(context_, device_name_, device_id);

    ma_device_config config  = ma_device_config_init(ma_device_type_playback);
    config.playback.pDeviceID = have_id ? &device_id : nullptr;
    config.playback.format    = ma_format_f32;
    config.playback.channels  = channels;
    config.sampleRate         = sample_rate;
    config.dataCallback       = data_callback;
    config.pUserData          = this;
    config.aaudio.usage       = ma_aaudio_usage_game;

    if (ma_device_init(&context_, &config, &device_) != MA_SUCCESS) {
        spdlog::get("illixr")->error("[audio_input] Failed to open the playback device for {} Hz, {} channel(s)", sample_rate,
                                     channels);
        ma_pcm_rb_uninit(&ring_);
        return false;
    }

    sample_rate_       = sample_rate;
    channels_          = channels;
    max_queued_frames_ = std::max<ma_uint32>(sample_rate * max_latency_ms_ / 1000, 1);
    prefill_frames_    = std::min<ma_uint32>(sample_rate * prefill_ms_ / 1000, max_queued_frames_);
    buffering_         = true; // the audio thread isn't running yet

    if (ma_device_start(&device_) != MA_SUCCESS) {
        spdlog::get("illixr")->error("[audio_input] Failed to start the playback device");
        ma_device_uninit(&device_);
        ma_pcm_rb_uninit(&ring_);
        return false;
    }

    device_ready_ = true;
    spdlog::get("illixr")->info("[audio_input] Playing on '{}' at {} Hz, {} channel(s)", device_.playback.name, sample_rate,
                                channels);
    return true;
}

void audio_input::close_device() {
    if (!device_ready_) {
        return;
    }

    // Stops the audio thread before the ring it reads from goes away.
    ma_device_uninit(&device_);
    ma_pcm_rb_uninit(&ring_);
    device_ready_ = false;
}

bool audio_input::write_frames(const float* source, ma_uint32 frame_count) {
    // All or nothing, so a dropped chunk shows up as a gap rather than a truncated chunk.
    if (ma_pcm_rb_available_write(&ring_) < frame_count) {
        return false;
    }

    ma_uint32 written = 0;
    while (written < frame_count) {
        ma_uint32 frames = frame_count - written;
        void*     buffer = nullptr;
        if (ma_pcm_rb_acquire_write(&ring_, &frames, &buffer) != MA_SUCCESS || frames == 0) {
            break;
        }

        std::memcpy(buffer, source + static_cast<size_t>(written) * channels_, static_cast<size_t>(frames) * channels_ * sizeof(float));
        ma_pcm_rb_commit_write(&ring_, frames);
        written += frames;
    }
    return true;
}

void audio_input::data_callback(ma_device* device, void* output, const void* input, ma_uint32 frame_count) {
    (void) input;
    static_cast<audio_input*>(device->pUserData)->playback(static_cast<float*>(output), frame_count);
}

void audio_input::playback(float* output, ma_uint32 frame_count) {
    // The output buffer arrives silenced, so anything not written below plays as silence.
    ma_uint32 queued = ma_pcm_rb_available_read(&ring_);
    if (queued > max_queued_frames_) {
        // The stream is ahead of the output clock (drift, or a burst after a network stall), drop back to the prefill level.
        ma_pcm_rb_seek_read(&ring_, queued - prefill_frames_);
        queued = prefill_frames_;
        latency_trims_.fetch_add(1, std::memory_order_relaxed);
    }

    if (buffering_) {
        if (queued == 0 || queued < prefill_frames_) {
            return;
        }
        buffering_ = false;
    }

    ma_uint32 played = 0;
    while (played < frame_count) {
        ma_uint32 frames = frame_count - played;
        void*     buffer = nullptr;
        if (ma_pcm_rb_acquire_read(&ring_, &frames, &buffer) != MA_SUCCESS || frames == 0) {
            break;
        }

        std::memcpy(output + static_cast<size_t>(played) * channels_, buffer, static_cast<size_t>(frames) * channels_ * sizeof(float));
        ma_pcm_rb_commit_read(&ring_, frames);
        played += frames;
    }

    if (played < frame_count) {
        // Ran dry, wait for the prefill again rather than stuttering chunk by chunk.
        buffering_ = true;
        underruns_.fetch_add(1, std::memory_order_relaxed);
    }
}

void audio_input::log_stats() {
    const auto now = std::chrono::steady_clock::now();
    if (now < next_stats_time_) {
        return;
    }
    next_stats_time_ = now + STATS_INTERVAL;

    // Only log when something went wrong since the last report.
    const uint64_t problems = gap_frames_ + ring_overflows_ + invalid_chunks_ + underruns_.load(std::memory_order_relaxed) +
        latency_trims_.load(std::memory_order_relaxed);
    if (problems == logged_problems_) {
        return;
    }
    logged_problems_ = problems;

    spdlog::get("illixr")->warn("[audio_input] Chunks {}, gap frames {}, underruns {}, latency trims {}, ring overflows {}, "
                                "invalid chunks {}, queued {} frames",
                                chunks_received_, gap_frames_, underruns_.load(), latency_trims_.load(), ring_overflows_,
                                invalid_chunks_, ma_pcm_rb_available_read(&ring_));
}

PLUGIN_MAIN(audio_input)
