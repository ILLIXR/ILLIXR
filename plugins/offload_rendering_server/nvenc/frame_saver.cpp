#pragma once
#ifdef DUMP_FRAMES
/// @file frame_saver.hpp
/// @brief Simple frame saver using PPM format (no external dependencies)
///
/// This version saves frames as PPM (Portable Pixmap) files which can be
/// viewed with most image viewers and easily converted to PNG with ImageMagick:
///   convert frame_000010.ppm frame_000010.png

    #include "frame_saver.hpp"

using namespace ILLIXR;

/// Simple frame saver that outputs PPM and raw formats (no external deps)

frame_saver::frame_saver(const config& cfg)
    : config_(cfg)
    , frame_count_(0) {
    if (config_.enabled) {
        // Create base directory
        create_directory(config_.output_directory);

        // Create session subdirectory with timestamp
        auto    now    = std::chrono::system_clock::now();
        auto    time_t = std::chrono::system_clock::to_time_t(now);
        std::tm tm_buf;
    #ifdef _WIN32
        localtime_s(&tm_buf, &time_t);
    #else
        localtime_r(&time_t, &tm_buf);
    #endif
        std::stringstream ss;
        ss << std::put_time(&tm_buf, "%Y%m%d_%H%M%S");
        session_dir_ = config_.output_directory + "/" + ss.str();
        create_directory(session_dir_);
    }
}

bool frame_saver::save_bgra(const uint8_t* data, uint32_t width, uint32_t height, size_t pitch, int eye,
                            const std::string& tag) {
    if (!config_.enabled)
        return true;

    frame_count_++;
    if (frame_count_ % config_.save_interval != 0) {
        return true;
    }

    if (pitch == 0)
        pitch = width * 4;

    std::string base_filename = build_filename(eye, tag);
    bool        success       = true;

    // Save as PPM (human-viewable format)
    if (config_.save_ppm) {
        success &= write_ppm_bgra(base_filename + ".ppm", data, width, height, pitch);
    }

    // Save as raw BGRA
    if (config_.save_raw) {
        success &= write_raw(base_filename + ".bgra", data, width, height, pitch);
    }

    return success;
}

bool frame_saver::save_rgba(const uint8_t* data, uint32_t width, uint32_t height, size_t pitch, int eye,
                            const std::string& tag) {
    if (!config_.enabled)
        return true;

    frame_count_++;
    if (frame_count_ % config_.save_interval != 0) {
        return true;
    }

    if (pitch == 0)
        pitch = width * 4;

    std::string base_filename = build_filename(eye, tag);
    bool        success       = true;

    if (config_.save_ppm) {
        success &= write_ppm_rgba(base_filename + ".ppm", data, width, height, pitch);
    }

    if (config_.save_raw) {
        success &= write_raw(base_filename + ".rgba", data, width, height, pitch);
    }

    return success;
}

bool frame_saver::save_nv12(const uint8_t* data, uint32_t width, uint32_t height, int eye, const std::string& tag) {
    if (!config_.enabled)
        return true;

    frame_count_++;
    if (frame_count_ % config_.save_interval != 0) {
        return true;
    }

    std::string base_filename = build_filename(eye, tag);
    bool        success       = true;

    if (config_.save_ppm) {
        // Convert NV12 to RGB for PPM
        std::vector<uint8_t> rgb = nv12_to_rgb(data, width, height);
        success &= write_ppm_rgb(base_filename + ".ppm", rgb.data(), width, height, width * 3);
    }

    if (config_.save_raw) {
        // Save raw NV12
        size_t        nv12_size = width * height * 3 / 2;
        std::ofstream file(session_dir_ + "/" + base_filename + ".nv12", std::ios::binary);
        if (file) {
            file.write(reinterpret_cast<const char*>(data), nv12_size);
            success &= file.good();
        } else {
            success = false;
        }
    }

    return success;
}

bool frame_saver::save_bitstream(const uint8_t* data, size_t size, int eye, const std::string& codec) {
    if (!config_.enabled)
        return true;
    if (frame_count_ % config_.save_interval != 0)
        return true;

    std::string   filename = build_filename(eye, codec) + ".bin";
    std::ofstream file(session_dir_ + "/" + filename, std::ios::binary);
    if (!file)
        return false;

    file.write(reinterpret_cast<const char*>(data), size);
    return file.good();
}

void frame_saver::tick() {
    if (config_.enabled)
        frame_count_++;
}

std::string frame_saver::build_filename(int eye, const std::string& tag) {
    std::stringstream ss;
    ss << config_.prefix << "_" << std::setfill('0') << std::setw(6) << frame_count_;
    if (eye >= 0) {
        ss << "_eye" << eye;
    }
    if (!tag.empty()) {
        ss << "_" << tag;
    }
    return ss.str();
}

bool frame_saver::write_ppm_bgra(const std::string& filename, const uint8_t* data, uint32_t width, uint32_t height,
                                 size_t pitch) {
    std::ofstream file(session_dir_ + "/" + filename, std::ios::binary);
    if (!file)
        return false;

    // PPM header
    file << "P6\n" << width << " " << height << "\n255\n";

    // Convert BGRA to RGB
    for (uint32_t y = 0; y < height; y++) {
        const uint8_t* row = data + y * pitch;
        for (uint32_t x = 0; x < width; x++) {
            uint8_t rgb[3] = {row[x * 4 + 2], row[x * 4 + 1], row[x * 4 + 0]}; // BGR -> RGB
            file.write(reinterpret_cast<char*>(rgb), 3);
        }
    }

    return file.good();
}

bool frame_saver::write_ppm_rgba(const std::string& filename, const uint8_t* data, uint32_t width, uint32_t height,
                                 size_t pitch) {
    std::ofstream file(session_dir_ + "/" + filename, std::ios::binary);
    if (!file)
        return false;

    file << "P6\n" << width << " " << height << "\n255\n";

    for (uint32_t y = 0; y < height; y++) {
        const uint8_t* row = data + y * pitch;
        for (uint32_t x = 0; x < width; x++) {
            file.write(reinterpret_cast<const char*>(row + x * 4), 3); // RGB (skip A)
        }
    }

    return file.good();
}

bool frame_saver::write_ppm_rgb(const std::string& filename, const uint8_t* data, uint32_t width, uint32_t height,
                                size_t pitch) {
    std::ofstream file(session_dir_ + "/" + filename, std::ios::binary);
    if (!file)
        return false;

    file << "P6\n" << width << " " << height << "\n255\n";

    for (uint32_t y = 0; y < height; y++) {
        file.write(reinterpret_cast<const char*>(data + y * pitch), width * 3);
    }

    return file.good();
}

bool frame_saver::write_raw(const std::string& filename, const uint8_t* data, uint32_t width, uint32_t height, size_t pitch) {
    std::ofstream file(session_dir_ + "/" + filename, std::ios::binary);
    if (!file)
        return false;

    // Write dimensions header
    file.write(reinterpret_cast<const char*>(&width), sizeof(width));
    file.write(reinterpret_cast<const char*>(&height), sizeof(height));

    // Write pixel data row by row
    for (uint32_t y = 0; y < height; y++) {
        file.write(reinterpret_cast<const char*>(data + y * pitch), width * 4);
    }

    return file.good();
}

std::vector<uint8_t> frame_saver::nv12_to_rgb(const uint8_t* data, uint32_t width, uint32_t height) {
    std::vector<uint8_t> rgb(width * height * 3);
    const uint8_t*       y_plane  = data;
    const uint8_t*       uv_plane = data + width * height;

    for (uint32_t j = 0; j < height; j++) {
        for (uint32_t i = 0; i < width; i++) {
            size_t y_idx  = j * width + i;
            size_t uv_idx = (j / 2) * width + (i & ~1u);

            int Y = y_plane[y_idx];
            int U = uv_plane[uv_idx] - 128;
            int V = uv_plane[uv_idx + 1] - 128;

            int R = Y + ((359 * V) >> 8);
            int G = Y - ((88 * U + 183 * V) >> 8);
            int B = Y + ((454 * U) >> 8);

            R = R < 0 ? 0 : (R > 255 ? 255 : R);
            G = G < 0 ? 0 : (G > 255 ? 255 : G);
            B = B < 0 ? 0 : (B > 255 ? 255 : B);

            size_t rgb_idx   = y_idx * 3;
            rgb[rgb_idx + 0] = static_cast<uint8_t>(R);
            rgb[rgb_idx + 1] = static_cast<uint8_t>(G);
            rgb[rgb_idx + 2] = static_cast<uint8_t>(B);
        }
    }

    return rgb;
}
#endif