#pragma once
#ifdef DUMP_FRAMES
/// @file frame_saver.hpp
/// @brief Simple frame saver using PPM format (no external dependencies)
///
/// This version saves frames as PPM (Portable Pixmap) files which can be
/// viewed with most image viewers and easily converted to PNG with ImageMagick:
///   convert frame_000010.ppm frame_000010.png

    #include <chrono>
    #include <cstdint>
    #include <cstring>
    #include <fstream>
    #include <iomanip>
    #include <spdlog/spdlog.h>
    #include <sstream>
    #include <string>
    #include <vector>

    #ifdef _WIN32
        #include <direct.h>
        #define MKDIR(dir) _mkdir(dir)
    #else
        #include <sys/stat.h>
        #define MKDIR(dir) mkdir(dir, 0755)
    #endif

namespace ILLIXR {

/// Simple frame saver that outputs PPM and raw formats (no external deps)
class frame_saver {
public:
    struct config {
        std::string output_directory = "saved_frames";
        int         save_interval    = 10;
        bool        enabled          = true;
        std::string prefix           = "frame";
        bool        save_ppm         = true;  ///< Save as PPM (viewable image format)
        bool        save_raw         = false; ///< Save as raw BGRA bytes
    };

    explicit frame_saver(const config& cfg = {});

    /// Save a BGRA frame (common Vulkan format)
    /// @param data Pointer to BGRA pixel data
    /// @param width Frame width
    /// @param height Frame height
    /// @param pitch Row pitch in bytes (0 = width * 4)
    /// @param eye Eye index (0=left, 1=right, -1=combined)
    /// @param tag Additional tag for filename
    /// @return true if saved or skipped, false on error
    bool save_bgra(const uint8_t* data, uint32_t width, uint32_t height, size_t pitch = 0, int eye = -1,
                   const std::string& tag = "");

    /// Save an RGBA frame
    bool save_rgba(const uint8_t* data, uint32_t width, uint32_t height, size_t pitch = 0, int eye = -1,
                   const std::string& tag = "");

    /// Save NV12 frame (decoder output)
    bool save_nv12(const uint8_t* data, uint32_t width, uint32_t height, int eye = -1, const std::string& tag = "");

    /// Save encoded bitstream
    bool save_bitstream(const uint8_t* data, size_t size, int eye = -1, const std::string& codec = "hevc");

    /// Increment frame counter without saving (for proper interval tracking)
    void tick();

    /// Check if next frame will be saved
    inline bool will_save_next() const {
        return config_.enabled && ((frame_count_ + 1) % config_.save_interval == 0);
    }

    inline uint64_t get_frame_count() const {
        return frame_count_;
    }

    inline const std::string& get_session_dir() const {
        return session_dir_;
    }

    inline void set_enabled(bool e) {
        config_.enabled = e;
    }

    inline bool is_enabled() const {
        return config_.enabled;
    }

    inline void increment_frame_count() {
        frame_count_++;
    }

private:
    std::string build_filename(int eye, const std::string& tag);

    static void create_directory(const std::string& path) {
        MKDIR(path.c_str());
    }

    // Write PPM from BGRA data
    bool write_ppm_bgra(const std::string& filename, const uint8_t* data, uint32_t width, uint32_t height, size_t pitch);

    // Write PPM from RGBA data
    bool write_ppm_rgba(const std::string& filename, const uint8_t* data, uint32_t width, uint32_t height, size_t pitch);

    // Write PPM from RGB data
    bool write_ppm_rgb(const std::string& filename, const uint8_t* data, uint32_t width, uint32_t height, size_t pitch);

    // Write raw pixel data
    bool write_raw(const std::string& filename, const uint8_t* data, uint32_t width, uint32_t height, size_t pitch);

    // Convert NV12 to RGB
    std::vector<uint8_t> nv12_to_rgb(const uint8_t* data, uint32_t width, uint32_t height);

    config      config_;
    std::string session_dir_;
    uint64_t    frame_count_;
};

} // namespace ILLIXR

// ============================================================================
// Usage Example for offload_rendering_server:
// ============================================================================
/*
#include "frame_saver.hpp"

class offload_rendering_server {
private:
    std::unique_ptr<ILLIXR::frame_saver> frame_saver_;

    void init_frame_saver() {
        ILLIXR::frame_saver::config cfg;
        cfg.output_directory = "server_frames";
        cfg.save_interval = 10;
        cfg.prefix = "server";
        cfg.save_ppm = true;
        cfg.save_raw = false;

        // Check environment variable
        if (const char* env = std::getenv("ILLIXR_SAVE_FRAMES")) {
            cfg.enabled = (std::string(env) == "1");
        }

        frame_saver_ = std::make_unique<ILLIXR::frame_saver>(cfg);

        if (cfg.enabled) {
            spdlog::info("Frame saver enabled, saving to: {}", frame_saver_->get_session_dir());
        }
    }

    // Call this in your encode loop after getting the frame from buffer pool
    void maybe_save_frame(const buffer_pool_image& image, int eye) {
        if (frame_saver_ && frame_saver_->will_save_next()) {
            // Need to read back the Vulkan image to CPU
            // See vulkan_frame_readback in frame_saver_integration.hpp
            std::vector<uint8_t> pixels = readback_vulkan_image(image);
            frame_saver_->save_bgra(pixels.data(), image.width, image.height, 0, eye, "color");
        }
    }
};
*/

#endif