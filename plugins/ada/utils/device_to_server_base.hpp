#pragma once
#include <condition_variable>
#include <fstream>
#include <opencv2/opencv.hpp>
#include <spdlog/spdlog.h>

namespace ILLIXR {

class device_to_server_base {
public:
    device_to_server_base() = default;

protected:
    [[maybe_unused]] static bool write_16bit_pgm(const cv::Mat& image, const std::string& filename) {
        // Check if the input image is 16-bit single-channel
        if (image.empty() || image.type() != CV_16U) {
            spdlog::get("illixr")->error("Input image must be non-empty and 16-bit single-channel.");
            return false;
        }

        std::ofstream file(filename, std::ios::binary | std::ios::out);
        if (!file.is_open()) {
            spdlog::get("illixr")->error("Failed to open file: {}", filename);
            return false;
        }

        // PGM header for a binary P5 type, 16-bit depth
        file << "P5\n";
        file << image.cols << " " << image.rows << "\n";
        file << "65535\n"; // Maximum value for 16-bit depth

        // Writing the image data
        for (int y = 0; y < image.rows; ++y) {
            for (int x = 0; x < image.cols; ++x) {
                auto pixel_value = image.at<uint16_t>(y, x);

                // Write the pixel value as big endian
                char high_byte = static_cast<char>(pixel_value >> 8);
                char low_byte  = static_cast<char>(pixel_value & 0xFF);
                file.write(&high_byte, 1);
                file.write(&low_byte, 1);
            }
        }

        file.close();
        return true;
    }

    unsigned frame_count_{0};
    unsigned fps_{15};

    std::mutex              mutex_;
    std::condition_variable cond_var_;
    bool                    img_ready_ = false;
};
} // namespace ILLIXR
