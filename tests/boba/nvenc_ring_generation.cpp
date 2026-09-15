#include "illixr/quest3_params.hpp"
#include "offload_rendering_server/nvenc/nvenc_encoder.hpp"

#include <fstream>
#include <iostream>
#include <spdlog/sinks/stdout_color_sinks.h>

int main(int argc, char** argv) {
    if (argc != 2)
        return 2;
    spdlog::stdout_color_mt("illixr");
    try {
        ILLIXR::nvenc_encoder encoder(NATIVE_STREAM_EYE_WIDTH * 2, NATIVE_STREAM_EYE_HEIGHT, 30000000, 72,
                                      ILLIXR::encoder_mode::color, ILLIXR::encoder_codec::av1);
        if (!encoder.initialize(ILLIXR::vulkan_context{}))
            return 3;
        constexpr unsigned   size = 1344;
        std::vector<uint8_t> left(size * size * 4, 255), right(left.size(), 255);
        for (unsigned y = 0; y < size; ++y) {
            for (unsigned x = 0; x < size; ++x) {
                const auto offset = (y * size + x) * 4;
                // Top: red/blue; bottom: green/yellow. Checks eye ordering and flip_y.
                left[offset]      = y < size / 2 ? 255 : 0;
                left[offset + 1]  = y < size / 2 ? 0 : 255;
                left[offset + 2]  = 0;
                right[offset]     = y < size / 2 ? 0 : 255;
                right[offset + 1] = y < size / 2 ? 0 : 255;
                right[offset + 2] = y < size / 2 ? 255 : 0;
            }
        }
        std::ofstream output(argv[1], std::ios::binary);
        if (!output)
            return 4;
        size_t total = 0;
        for (int i = 0; i < 12; ++i) {
            if (i == 6)
                encoder.request_idr();
            // Reject a recycled source after GPU upload. This must not consume
            // an encoder reference picture or change the following frame's GOP.
            bool validated = false;
            auto rejected  = encoder.encode_rgba_stereo(left.data(), size * 4, right.data(), size * 4, size, size, true, [&] {
                validated = true;
                return false;
            });
            if (!validated || !rejected.empty())
                return 7;
            auto bytes = encoder.encode_rgba_stereo(left.data(), size * 4, right.data(), size * 4, size, size, true);
            if (bytes.empty())
                return 5;
            if ((i == 0 || i == 6) && !encoder.last_frame_was_keyframe())
                return 8;
            output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            total += bytes.size();
        }
        std::cout << "NVENC_SMOKE_PASS frames=12 bytes=" << total << " output=" << NATIVE_STREAM_EYE_WIDTH * 2 << 'x'
                  << NATIVE_STREAM_EYE_HEIGHT << '\n';
        return output ? 0 : 6;
    } catch (const std::exception& error) {
        std::cerr << "NVENC_SMOKE_FAIL " << error.what() << '\n';
        return 1;
    }
}
