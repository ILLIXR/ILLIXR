#pragma once
#ifdef ILLIXR_DUMP_AUDIO
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <string>
#include <system_error>

namespace ILLIXR {

/**
 * @brief Temporary debugging aid: appends audio to 32-bit float WAV files.
 *
 * Disabled when constructed with an empty directory. Files are named
 * <prefix>_<unix seconds>_<index>_f<start frame>.wav. A new one starts on the first write, when
 * the sample rate or channel count changes, when the stream is not contiguous (a gap or restart in
 * start_frame), and when split() is called, so every file is contiguous and the start frame in its
 * name lines it up with the matching dump on the other side of the network. The header sizes are
 * rewritten after every write, so a file stays playable if the process is killed. Not thread safe;
 * each instance must be written from one thread at a time.
 */
class audio_dump {
public:
    audio_dump(std::string directory, std::string prefix)
        : directory_{std::move(directory)}
        , prefix_{std::move(prefix)} { }

    audio_dump(const audio_dump&)            = delete;
    audio_dump& operator=(const audio_dump&) = delete;

    ~audio_dump() {
        close();
    }

    [[nodiscard]] bool enabled() const {
        return !directory_.empty();
    }

    /**
     * @brief Appends interleaved samples, starting a new file if the format changed or the stream jumped.
     */
    void write(const float* samples, uint32_t frame_count, uint32_t channels, uint32_t sample_rate, uint64_t start_frame) {
        if (!enabled() || failed_ || frame_count == 0 || channels == 0 || sample_rate == 0) {
            return;
        }

        const uint64_t bytes = static_cast<uint64_t>(frame_count) * channels * sizeof(float);
        if (file_ != nullptr &&
            (channels != channels_ || sample_rate != sample_rate_ || start_frame != next_frame_ || data_bytes_ + bytes > MAX_DATA_BYTES)) {
            close();
        }
        if (file_ == nullptr && !open(channels, sample_rate, start_frame)) {
            spdlog::get("illixr")->error("[audio_dump] failed open");
            return;
        }

        std::fseek(file_, 0, SEEK_END);
        std::fwrite(samples, 1, static_cast<size_t>(bytes), file_);
        data_bytes_ += bytes;
        next_frame_ = start_frame + frame_count;
        write_sizes();
    }

    /**
     * @brief Ends the current file; the next write starts a new one.
     */
    void split() {
        close();
    }

private:
    static constexpr uint64_t MAX_DATA_BYTES = 0xFFFFFFFFull - 64; // RIFF sizes are 32 bit
    static constexpr uint16_t FORMAT_FLOAT   = 3;                  // WAVE_FORMAT_IEEE_FLOAT

    bool open(uint32_t channels, uint32_t sample_rate, uint64_t start_frame) {
        std::error_code error;
        std::filesystem::create_directories(directory_, error);

        const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        const std::filesystem::path path =
            std::filesystem::path{directory_} / (prefix_ + "_" + std::to_string(seconds) + "_" + std::to_string(file_index_++) + "_f" + std::to_string(start_frame) + ".wav");
        spdlog::get("illixr")->info("[audio_dump] opening {}", path.string());
        file_ = std::fopen(path.string().c_str(), "wb+");
        if (file_ == nullptr) {
            // Only report once, rather than once per chunk.
            spdlog::get("illixr")->error("[audio_dump] Cannot create {}, dumping disabled", path.string());
            failed_ = true;
            return false;
        }

        channels_    = channels;
        sample_rate_ = sample_rate;
        data_bytes_  = 0;

        const uint32_t byte_rate   = sample_rate * channels * static_cast<uint32_t>(sizeof(float));
        const uint16_t block_align = static_cast<uint16_t>(channels * sizeof(float));

        std::fwrite("RIFF", 1, 4, file_);
        put_u32(36); // patched by write_sizes()
        std::fwrite("WAVEfmt ", 1, 8, file_);
        put_u32(16);
        put_u16(FORMAT_FLOAT);
        put_u16(static_cast<uint16_t>(channels));
        put_u32(sample_rate);
        put_u32(byte_rate);
        put_u16(block_align);
        put_u16(32);
        std::fwrite("data", 1, 4, file_);
        put_u32(0); // patched by write_sizes()
        std::fflush(file_);

        spdlog::get("illixr")->info("[audio_dump] Writing {} ({} Hz, {} channel(s))", path.string(), sample_rate, channels);
        return true;
    }

    void close() {
        if (file_ != nullptr) {
            std::fclose(file_);
            file_ = nullptr;
        }
    }

    void write_sizes() {
        const auto data_size = static_cast<uint32_t>(data_bytes_);
        std::fseek(file_, 4, SEEK_SET);
        put_u32(36 + data_size);
        std::fseek(file_, 40, SEEK_SET);
        put_u32(data_size);
        std::fflush(file_);
    }

    // WAV fields are little endian, written byte by byte so the host's byte order doesn't matter.
    void put_u16(uint16_t value) {
        const unsigned char bytes[2] = {static_cast<unsigned char>(value), static_cast<unsigned char>(value >> 8)};
        std::fwrite(bytes, 1, 2, file_);
    }

    void put_u32(uint32_t value) {
        const unsigned char bytes[4] = {static_cast<unsigned char>(value), static_cast<unsigned char>(value >> 8),
                                        static_cast<unsigned char>(value >> 16), static_cast<unsigned char>(value >> 24)};
        std::fwrite(bytes, 1, 4, file_);
    }

    const std::string directory_;
    const std::string prefix_;
    std::FILE*        file_        = nullptr;
    uint32_t          channels_    = 0;
    uint32_t          sample_rate_ = 0;
    uint64_t          data_bytes_  = 0;
    uint64_t          next_frame_  = 0;
    unsigned          file_index_  = 0;
    bool              failed_      = false;
};

} // namespace ILLIXR
#endif
