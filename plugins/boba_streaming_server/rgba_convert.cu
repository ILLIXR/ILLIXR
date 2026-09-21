// Boba host RGBA conversion; compiled only by boba_streaming_server.
#include <cstdint>
#include <cuda_runtime.h>

namespace {
// BT.709 coefficients for RGB to YUV conversion
// Y  =  0.2126 * R + 0.7152 * G + 0.0722 * B
// Cb = -0.1146 * R - 0.3854 * G + 0.5000 * B + 128
// Cr =  0.5000 * R - 0.4542 * G - 0.0458 * B + 128

// Scaled coefficients (multiplied by 256 for integer math)
#define Y_R 54  // 0.2126 * 256
#define Y_G 183 // 0.7152 * 256
#define Y_B 18  // 0.0722 * 256

#define U_R -29 // -0.1146 * 256
#define U_G -99 // -0.3854 * 256
#define U_B 128 // 0.5000 * 256

#define V_R 128  // 0.5000 * 256
#define V_G -116 // -0.4542 * 256
#define V_B -12  // -0.0458 * 256

// Device-side min/max to avoid conflicts with Windows macros
__device__ __forceinline__ int device_min(int a, int b) {
    return (a < b) ? a : b;
}

__device__ __forceinline__ int device_max(int a, int b) {
    return (a > b) ? a : b;
}

__device__ __forceinline__ int device_clamp(int val, int lo, int hi) {
    return device_max(lo, device_min(hi, val));
}

// ---------------------------------------------------------------------------
// Host-originated Boba RGBA stereo conversion
// ---------------------------------------------------------------------------
// Unlike the existing Vulkan-texture path, Boba supplies two linear row-pitched
// RGBA8 buffers. Sampling and conversion are fused so resizing adds no extra GPU
// surface or kernel launch.
struct rgb_sample {
    float r;
    float g;
    float b;
};

__device__ __forceinline__ float bilinear_channel(const uint8_t* p00, const uint8_t* p10, const uint8_t* p01,
                                                  const uint8_t* p11, int channel, float tx, float ty) {
    const float top =
        static_cast<float>(p00[channel]) + tx * (static_cast<float>(p10[channel]) - static_cast<float>(p00[channel]));
    const float bottom =
        static_cast<float>(p01[channel]) + tx * (static_cast<float>(p11[channel]) - static_cast<float>(p01[channel]));
    return top + ty * (bottom - top);
}

__device__ __forceinline__ rgb_sample sample_rgba_bilinear(const uint8_t* source, size_t pitch, uint32_t width, uint32_t height,
                                                           float u, float v, bool flip_y) {
    u = fminf(fmaxf(u, 0.0f), 1.0f);
    v = fminf(fmaxf(v, 0.0f), 1.0f);
    if (flip_y) {
        v = 1.0f - v;
    }

    const float source_x = u * static_cast<float>(width) - 0.5f;
    const float source_y = v * static_cast<float>(height) - 0.5f;
    const int   x0       = device_clamp(static_cast<int>(floorf(source_x)), 0, static_cast<int>(width) - 1);
    const int   y0       = device_clamp(static_cast<int>(floorf(source_y)), 0, static_cast<int>(height) - 1);
    const int   x1       = device_min(x0 + 1, static_cast<int>(width) - 1);
    const int   y1       = device_min(y0 + 1, static_cast<int>(height) - 1);
    const float tx       = fminf(fmaxf(source_x - floorf(source_x), 0.0f), 1.0f);
    const float ty       = fminf(fmaxf(source_y - floorf(source_y), 0.0f), 1.0f);

    const uint8_t* p00 = source + static_cast<size_t>(y0) * pitch + static_cast<size_t>(x0) * 4;
    const uint8_t* p10 = source + static_cast<size_t>(y0) * pitch + static_cast<size_t>(x1) * 4;
    const uint8_t* p01 = source + static_cast<size_t>(y1) * pitch + static_cast<size_t>(x0) * 4;
    const uint8_t* p11 = source + static_cast<size_t>(y1) * pitch + static_cast<size_t>(x1) * 4;

    // Boba's shared images are RGBA8, unlike the Vulkan BGRA texture path.
    return {bilinear_channel(p00, p10, p01, p11, 0, tx, ty), bilinear_channel(p00, p10, p01, p11, 1, tx, ty),
            bilinear_channel(p00, p10, p01, p11, 2, tx, ty)};
}

__global__ void rgba_stereo_linear_to_nv12_kernel(const uint8_t* left_rgba, size_t left_pitch, const uint8_t* right_rgba,
                                                  size_t right_pitch, uint32_t source_width, uint32_t source_height,
                                                  uint8_t* __restrict__ y_dst, uint8_t* __restrict__ uv_dst, size_t dst_pitch,
                                                  uint32_t dst_eye_width, uint32_t dst_height, bool flip_y) {
    const uint32_t x = blockIdx.x * blockDim.x + threadIdx.x;
    const uint32_t y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= dst_eye_width * 2 || y >= dst_height) {
        return;
    }

    const bool       right   = x >= dst_eye_width;
    const uint32_t   local_x = right ? x - dst_eye_width : x;
    const uint8_t*   source  = right ? right_rgba : left_rgba;
    const size_t     pitch   = right ? right_pitch : left_pitch;
    const float      u       = (static_cast<float>(local_x) + 0.5f) / static_cast<float>(dst_eye_width);
    const float      v       = (static_cast<float>(y) + 0.5f) / static_cast<float>(dst_height);
    const rgb_sample pixel   = sample_rgba_bilinear(source, pitch, source_width, source_height, u, v, flip_y);

    const int r = static_cast<int>(pixel.r + 0.5f);
    const int g = static_cast<int>(pixel.g + 0.5f);
    const int b = static_cast<int>(pixel.b + 0.5f);
    y_dst[static_cast<size_t>(y) * dst_pitch + x] =
        static_cast<uint8_t>(device_clamp((Y_R * r + Y_G * g + Y_B * b + 128) >> 8, 0, 255));

    // NV12 shares one interleaved UV pair across a 2x2 luma block. Average four
    // bilinear samples instead of reusing the upper-left pixel's chroma.
    if ((x & 1U) == 0 && (y & 1U) == 0) {
        float r_sum = 0.0f;
        float g_sum = 0.0f;
        float b_sum = 0.0f;
        int   count = 0;
        for (uint32_t dy = 0; dy < 2 && y + dy < dst_height; ++dy) {
            for (uint32_t dx = 0; dx < 2 && local_x + dx < dst_eye_width; ++dx) {
                const float      sample_u = (static_cast<float>(local_x + dx) + 0.5f) / static_cast<float>(dst_eye_width);
                const float      sample_v = (static_cast<float>(y + dy) + 0.5f) / static_cast<float>(dst_height);
                const rgb_sample sample =
                    sample_rgba_bilinear(source, pitch, source_width, source_height, sample_u, sample_v, flip_y);
                r_sum += sample.r;
                g_sum += sample.g;
                b_sum += sample.b;
                ++count;
            }
        }
        const int    r_avg    = static_cast<int>(r_sum / static_cast<float>(count) + 0.5f);
        const int    g_avg    = static_cast<int>(g_sum / static_cast<float>(count) + 0.5f);
        const int    b_avg    = static_cast<int>(b_sum / static_cast<float>(count) + 0.5f);
        const int    u_value  = device_clamp(((U_R * r_avg + U_G * g_avg + U_B * b_avg + 128) >> 8) + 128, 0, 255);
        const int    v_value  = device_clamp(((V_R * r_avg + V_G * g_avg + V_B * b_avg + 128) >> 8) + 128, 0, 255);
        const size_t uv_index = static_cast<size_t>(y / 2) * dst_pitch + x;
        uv_dst[uv_index]      = static_cast<uint8_t>(u_value);
        uv_dst[uv_index + 1]  = static_cast<uint8_t>(v_value);
    }
}

} // namespace

extern "C" {
cudaError_t launch_rgba_stereo_linear_to_nv12(const uint8_t* left_rgba, size_t left_pitch, const uint8_t* right_rgba,
                                              size_t right_pitch, uint32_t source_width, uint32_t source_height,
                                              uint8_t* dst_nv12, size_t dst_pitch, uint32_t dst_eye_width, uint32_t dst_height,
                                              uint32_t aligned_height, bool flip_y, cudaStream_t stream) {
    uint8_t*   y_plane  = dst_nv12;
    uint8_t*   uv_plane = dst_nv12 + dst_pitch * aligned_height;
    const dim3 block(16, 16);
    const dim3 grid(((dst_eye_width * 2) + block.x - 1) / block.x, (dst_height + block.y - 1) / block.y);
    rgba_stereo_linear_to_nv12_kernel<<<grid, block, 0, stream>>>(left_rgba, left_pitch, right_rgba, right_pitch, source_width,
                                                                  source_height, y_plane, uv_plane, dst_pitch, dst_eye_width,
                                                                  dst_height, flip_y);
    return cudaGetLastError();
}

} // extern "C"
