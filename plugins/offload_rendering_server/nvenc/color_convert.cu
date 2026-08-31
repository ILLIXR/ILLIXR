/**
 * @file color_convert.cu
 * @brief CUDA kernels for BGRA to NV12 color conversion
 *
 * This file contains optimized CUDA kernels for converting BGRA images
 * to NV12 format for NVENC encoding. Uses BT.709 color space coefficients.
 */

// cuda_runtime.h provides all required device-side types and texture-fetch
// intrinsics on every platform (Windows, Linux) and every CUDA version.
// The Windows-specific sub-headers (device_launch_parameters.h,
// texture_fetch_functions.h, cuda_texture_types.h) are fully subsumed by
// cuda_runtime.h and must NOT be included separately: they were deprecated in
// CUDA 11 and removed in CUDA 13.
#include <cstdint>
#include <cuda_runtime.h>

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

/**
 * @brief Convert BGRA to Y plane (full resolution)
 *
 * Each thread processes one pixel.
 *
 * @param src Source BGRA data (linear memory)
 * @param dst Destination Y plane
 * @param src_pitch Source pitch in bytes
 * @param dst_pitch Destination pitch in bytes
 * @param width Image width
 * @param height Image height
 */
__global__ void bgra_to_y_kernel(const uint8_t* __restrict__ src, uint8_t* __restrict__ dst, size_t src_pitch, size_t dst_pitch,
                                 uint32_t width, uint32_t height) {
    const uint32_t x = blockIdx.x * blockDim.x + threadIdx.x;
    const uint32_t y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x >= width || y >= height) {
        return;
    }

    // Read BGRA pixel
    const size_t  src_idx = y * src_pitch + x * 4;
    const uint8_t b       = src[src_idx + 0];
    const uint8_t g       = src[src_idx + 1];
    const uint8_t r       = src[src_idx + 2];
    // Alpha (src[src_idx + 3]) is ignored

    // Convert to Y using BT.709 coefficients
    // Y = 16 + (65.481 * R + 128.553 * G + 24.966 * B) / 255
    // Simplified: Y = (Y_R * R + Y_G * G + Y_B * B + 128) >> 8
    int y_val = ((Y_R * r + Y_G * g + Y_B * b + 128) >> 8);

    // Clamp to valid range [16, 235] for limited range, or [0, 255] for full range
    // Using full range for VR content
    y_val = device_clamp(y_val, 0, 255);

    // Write Y value
    dst[y * dst_pitch + x] = static_cast<uint8_t>(y_val);
}

/**
 * @brief Convert BGRA to UV plane (2x2 subsampled)
 *
 * Each thread processes a 2x2 block of pixels and outputs one UV pair.
 *
 * @param src Source BGRA data (linear memory)
 * @param dst Destination UV plane (interleaved NV12 format)
 * @param src_pitch Source pitch in bytes
 * @param dst_pitch Destination pitch in bytes
 * @param width Image width (full resolution)
 * @param height Image height (full resolution)
 */
__global__ void bgra_to_uv_kernel(const uint8_t* __restrict__ src, uint8_t* __restrict__ dst, size_t src_pitch,
                                  size_t dst_pitch, uint32_t width, uint32_t height) {
    const uint32_t x = blockIdx.x * blockDim.x + threadIdx.x; // UV x coordinate
    const uint32_t y = blockIdx.y * blockDim.y + threadIdx.y; // UV y coordinate

    // UV plane is half resolution
    if (x >= width / 2 || y >= height / 2) {
        return;
    }

    // Sample 2x2 block and average
    int r_sum = 0, g_sum = 0, b_sum = 0;

#pragma unroll
    for (int dy = 0; dy < 2; dy++) {
#pragma unroll
        for (int dx = 0; dx < 2; dx++) {
            const uint32_t src_x = x * 2 + dx;
            const uint32_t src_y = y * 2 + dy;

            if (src_x < width && src_y < height) {
                const size_t src_idx = src_y * src_pitch + src_x * 4;
                b_sum += src[src_idx + 0];
                g_sum += src[src_idx + 1];
                r_sum += src[src_idx + 2];
            }
        }
    }

    // Average (divide by 4)
    const int r = r_sum >> 2;
    const int g = g_sum >> 2;
    const int b = b_sum >> 2;

    // Convert to U (Cb) and V (Cr) using BT.709 coefficients
    int u_val = ((U_R * r + U_G * g + U_B * b + 128) >> 8) + 128;
    int v_val = ((V_R * r + V_G * g + V_B * b + 128) >> 8) + 128;

    // Clamp to valid range
    u_val = device_clamp(u_val, 0, 255);
    v_val = device_clamp(v_val, 0, 255);

    // Write UV pair (NV12 format: U, V interleaved)
    const size_t dst_idx = y * dst_pitch + x * 2;
    dst[dst_idx + 0]     = static_cast<uint8_t>(u_val);
    dst[dst_idx + 1]     = static_cast<uint8_t>(v_val);
}

/**
 * @brief Combined BGRA to NV12 conversion using texture memory
 *
 * This kernel uses texture memory for better cache performance when
 * reading from CUDA arrays (imported Vulkan images).
 *
 * The texture object MUST be created with normalizedCoords=1 and bilinear
 * filtering (cudaFilterModeLinear). Pixel-center coordinates are therefore
 * expressed as (x + 0.5) / width rather than x + 0.5.
 *
 * @param tex_obj   Texture object bound to source BGRA CUDA array
 *                  (normalizedCoords=1, filterMode=Linear, readMode=NormalizedFloat)
 * @param y_dst     Destination Y plane
 * @param uv_dst    Destination UV plane
 * @param dst_pitch Destination pitch in bytes
 * @param width     Image width  (source == destination for this 1:1 kernel)
 * @param height    Image height (source == destination for this 1:1 kernel)
 */
__global__ void bgra_texture_to_nv12_kernel(cudaTextureObject_t tex_obj, uint8_t* __restrict__ y_dst,
                                            uint8_t* __restrict__ uv_dst, size_t dst_pitch, uint32_t width, uint32_t height) {
    const uint32_t x = blockIdx.x * blockDim.x + threadIdx.x;
    const uint32_t y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x >= width || y >= height) {
        return;
    }

    // Normalized pixel-center UV coordinates (texture uses normalizedCoords=1)
    const float inv_w = 1.0f / static_cast<float>(width);
    const float inv_h = 1.0f / static_cast<float>(height);
    const float u     = (static_cast<float>(x) + 0.5f) * inv_w;
    const float v     = (static_cast<float>(y) + 0.5f) * inv_h;

    // Read BGRA from texture (normalized float4).
    // Note: channels are mapped as .x=R, .y=G, .z=B (corrected from raw BGRA order).
    float4 pixel = tex2D<float4>(tex_obj, u, v);

    // Convert from normalized [0,1] to [0,255]
    const int r = static_cast<int>(pixel.x * 255.0f + 0.5f);
    const int g = static_cast<int>(pixel.y * 255.0f + 0.5f);
    const int b = static_cast<int>(pixel.z * 255.0f + 0.5f);

    // Convert to Y
    int y_val                = ((Y_R * r + Y_G * g + Y_B * b + 128) >> 8);
    y_val                    = device_clamp(y_val, 0, 255);
    y_dst[y * dst_pitch + x] = static_cast<uint8_t>(y_val);

    // UV is 2x2 subsampled - only process if we're at an even coordinate
    if ((x & 1) == 0 && (y & 1) == 0) {
        // Sample the remaining three pixels of the 2x2 block using normalized coords
        int r_sum = r, g_sum = g, b_sum = b;

        if (x + 1 < width) {
            float4 p1 = tex2D<float4>(tex_obj, (static_cast<float>(x) + 1.5f) * inv_w, v);
            r_sum += static_cast<int>(p1.x * 255.0f + 0.5f);
            g_sum += static_cast<int>(p1.y * 255.0f + 0.5f);
            b_sum += static_cast<int>(p1.z * 255.0f + 0.5f);
        }
        if (y + 1 < height) {
            float4 p2 = tex2D<float4>(tex_obj, u, (static_cast<float>(y) + 1.5f) * inv_h);
            r_sum += static_cast<int>(p2.x * 255.0f + 0.5f);
            g_sum += static_cast<int>(p2.y * 255.0f + 0.5f);
            b_sum += static_cast<int>(p2.z * 255.0f + 0.5f);
        }
        if (x + 1 < width && y + 1 < height) {
            float4 p3 = tex2D<float4>(tex_obj, (static_cast<float>(x) + 1.5f) * inv_w, (static_cast<float>(y) + 1.5f) * inv_h);
            r_sum += static_cast<int>(p3.x * 255.0f + 0.5f);
            g_sum += static_cast<int>(p3.y * 255.0f + 0.5f);
            b_sum += static_cast<int>(p3.z * 255.0f + 0.5f);
        }

        // Average
        const int r_avg = r_sum >> 2;
        const int g_avg = g_sum >> 2;
        const int b_avg = b_sum >> 2;

        // Convert to UV
        int u_val = ((U_R * r_avg + U_G * g_avg + U_B * b_avg + 128) >> 8) + 128;
        int v_val = ((V_R * r_avg + V_G * g_avg + V_B * b_avg + 128) >> 8) + 128;
        u_val     = device_clamp(u_val, 0, 255);
        v_val     = device_clamp(v_val, 0, 255);

        // Write UV pair
        const size_t uv_x   = x >> 1;
        const size_t uv_y   = y >> 1;
        const size_t uv_idx = uv_y * dst_pitch + uv_x * 2;
        uv_dst[uv_idx + 0]  = static_cast<uint8_t>(u_val);
        uv_dst[uv_idx + 1]  = static_cast<uint8_t>(v_val);
    }
}

/**
 * @brief Scaled BGRA to NV12 conversion using texture memory
 *
 * Identical in structure to bgra_texture_to_nv12_kernel but iterates over
 * DST_WIDTH x DST_HEIGHT output pixels instead of source pixels.  This lets
 * the kernel transparently handle any source-to-destination scale ratio
 * (downscale, upscale, or 1:1) with no extra passes or temporary buffers.
 *
 * Each output pixel samples the source texture at normalized coordinates
 * (dst_x + 0.5) / dst_width, (dst_y + 0.5) / dst_height.  When the source
 * is larger than the destination (e.g. Monado renders at 140% scale), the
 * hardware bilinear filter naturally downsamples.
 *
 * UV chroma averaging samples the 2x2 block in DESTINATION space so that
 * the 4:2:0 subsampling remains consistent regardless of scale.
 *
 * The texture object MUST be created with normalizedCoords=1 and bilinear
 * filtering (cudaFilterModeLinear).
 *
 * @param tex_obj    Texture object bound to source BGRA CUDA array
 *                   (normalizedCoords=1, filterMode=Linear, readMode=NormalizedFloat)
 * @param y_dst      Destination Y plane
 * @param uv_dst     Destination UV plane
 * @param dst_pitch  Destination pitch in bytes
 * @param dst_width  Width  of the desired output image in pixels
 * @param dst_height Height of the desired output image in pixels
 */
__global__ void bgra_texture_to_nv12_scaled_kernel(cudaTextureObject_t tex_obj, uint8_t* __restrict__ y_dst,
                                                   uint8_t* __restrict__ uv_dst, size_t dst_pitch, uint32_t dst_width,
                                                   uint32_t dst_height) {
    const uint32_t x = blockIdx.x * blockDim.x + threadIdx.x;
    const uint32_t y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x >= dst_width || y >= dst_height) {
        return;
    }

    // Normalized pixel-center UV coordinates mapped to destination grid.
    // When src > dst the hardware bilinear filter interpolates across multiple
    // source pixels, providing a correct downsample for free.
    const float inv_w = 1.0f / static_cast<float>(dst_width);
    const float inv_h = 1.0f / static_cast<float>(dst_height);
    const float u     = (static_cast<float>(x) + 0.5f) * inv_w;
    const float v     = (static_cast<float>(y) + 0.5f) * inv_h;

    // Read source pixel. Channel mapping .x=R, .y=G, .z=B matches the original
    // corrected order from bgra_texture_to_nv12_kernel.
    float4 pixel = tex2D<float4>(tex_obj, u, v);

    // Convert from normalized [0,1] to [0,255]
    const int r = static_cast<int>(pixel.x * 255.0f + 0.5f);
    const int g = static_cast<int>(pixel.y * 255.0f + 0.5f);
    const int b = static_cast<int>(pixel.z * 255.0f + 0.5f);

    // Convert to Y
    int y_val                = ((Y_R * r + Y_G * g + Y_B * b + 128) >> 8);
    y_val                    = device_clamp(y_val, 0, 255);
    y_dst[y * dst_pitch + x] = static_cast<uint8_t>(y_val);

    // UV is 2x2 subsampled - only process if we're at an even coordinate
    if ((x & 1) == 0 && (y & 1) == 0) {
        // Sample the remaining three pixels of the 2x2 destination block.
        // Clamp to the destination boundary on odd-sized images.
        int r_sum = r, g_sum = g, b_sum = b;

        if (x + 1 < dst_width) {
            float4 p1 = tex2D<float4>(tex_obj, (static_cast<float>(x) + 1.5f) * inv_w, v);
            r_sum += static_cast<int>(p1.x * 255.0f + 0.5f);
            g_sum += static_cast<int>(p1.y * 255.0f + 0.5f);
            b_sum += static_cast<int>(p1.z * 255.0f + 0.5f);
        }
        if (y + 1 < dst_height) {
            float4 p2 = tex2D<float4>(tex_obj, u, (static_cast<float>(y) + 1.5f) * inv_h);
            r_sum += static_cast<int>(p2.x * 255.0f + 0.5f);
            g_sum += static_cast<int>(p2.y * 255.0f + 0.5f);
            b_sum += static_cast<int>(p2.z * 255.0f + 0.5f);
        }
        if (x + 1 < dst_width && y + 1 < dst_height) {
            float4 p3 = tex2D<float4>(tex_obj, (static_cast<float>(x) + 1.5f) * inv_w, (static_cast<float>(y) + 1.5f) * inv_h);
            r_sum += static_cast<int>(p3.x * 255.0f + 0.5f);
            g_sum += static_cast<int>(p3.y * 255.0f + 0.5f);
            b_sum += static_cast<int>(p3.z * 255.0f + 0.5f);
        }

        // Average
        const int r_avg = r_sum >> 2;
        const int g_avg = g_sum >> 2;
        const int b_avg = b_sum >> 2;

        // Convert to UV
        int u_val = ((U_R * r_avg + U_G * g_avg + U_B * b_avg + 128) >> 8) + 128;
        int v_val = ((V_R * r_avg + V_G * g_avg + V_B * b_avg + 128) >> 8) + 128;
        u_val     = device_clamp(u_val, 0, 255);
        v_val     = device_clamp(v_val, 0, 255);

        // Write UV pair
        const size_t uv_x   = x >> 1;
        const size_t uv_y   = y >> 1;
        const size_t uv_idx = uv_y * dst_pitch + uv_x * 2;
        uv_dst[uv_idx + 0]  = static_cast<uint8_t>(u_val);
        uv_dst[uv_idx + 1]  = static_cast<uint8_t>(v_val);
    }
}

// ============================================================================
// Host-callable wrapper functions
// ============================================================================

extern "C" {

/**
 * @brief Launch BGRA to NV12 conversion from linear memory
 */
cudaError_t launch_bgra_to_nv12(const uint8_t* src_bgra, uint8_t* dst_nv12, size_t src_pitch, size_t dst_pitch, uint32_t width,
                                uint32_t height, uint32_t aligned_height, cudaStream_t stream) {
    // Y plane kernel
    dim3 block_y(16, 16);
    dim3 grid_y((width + block_y.x - 1) / block_y.x, (height + block_y.y - 1) / block_y.y);

    bgra_to_y_kernel<<<grid_y, block_y, 0, stream>>>(src_bgra, dst_nv12, src_pitch, dst_pitch, width, height);

    // UV plane kernel (half resolution)
    uint8_t* uv_plane = dst_nv12 + dst_pitch * aligned_height;
    dim3     block_uv(16, 16);
    dim3     grid_uv((width / 2 + block_uv.x - 1) / block_uv.x, (height / 2 + block_uv.y - 1) / block_uv.y);

    bgra_to_uv_kernel<<<grid_uv, block_uv, 0, stream>>>(src_bgra, uv_plane, src_pitch, dst_pitch, width, height);

    return cudaGetLastError();
}

/**
 * @brief Launch BGRA to NV12 conversion from texture (CUDA array), 1:1 resolution
 *
 * The texture object must be created with normalizedCoords=1 and bilinear
 * filtering so that pixel-center coordinates are expressed as
 * (x + 0.5) / width rather than x + 0.5.
 */
cudaError_t launch_bgra_texture_to_nv12(cudaTextureObject_t tex_obj, uint8_t* dst_nv12, size_t dst_pitch, uint32_t width,
                                        uint32_t height, uint32_t aligned_height, cudaStream_t stream) {
    uint8_t* y_plane  = dst_nv12;
    uint8_t* uv_plane = dst_nv12 + dst_pitch * aligned_height;

    dim3 block(16, 16);
    dim3 grid((width + block.x - 1) / block.x, (height + block.y - 1) / block.y);

    bgra_texture_to_nv12_kernel<<<grid, block, 0, stream>>>(tex_obj, y_plane, uv_plane, dst_pitch, width, height);

    return cudaGetLastError();
}

/**
 * @brief Launch scaled BGRA to NV12 conversion from texture (CUDA array)
 *
 * Downsamples (or upsamples) the source texture to dst_width x dst_height
 * output pixels using hardware bilinear filtering.  No extra passes or
 * temporary buffers are required.
 *
 * Use this instead of launch_bgra_texture_to_nv12 whenever the Vulkan source
 * images are larger than the encode target (e.g. XRT_COMPOSITOR_SCALE_PERCENTAGE=140).
 * At 1:1 scale the result is identical to the non-scaled variant.
 *
 * The texture object must be created with normalizedCoords=1 and bilinear
 * filtering (cudaFilterModeLinear).
 */
cudaError_t launch_bgra_texture_to_nv12_scaled(cudaTextureObject_t tex_obj, uint8_t* dst_nv12, size_t dst_pitch,
                                               uint32_t dst_width, uint32_t dst_height, uint32_t aligned_height,
                                               cudaStream_t stream) {
    uint8_t* y_plane  = dst_nv12;
    uint8_t* uv_plane = dst_nv12 + dst_pitch * aligned_height;

    dim3 block(16, 16);
    dim3 grid((dst_width + block.x - 1) / block.x, (dst_height + block.y - 1) / block.y);

    bgra_texture_to_nv12_scaled_kernel<<<grid, block, 0, stream>>>(tex_obj, y_plane, uv_plane, dst_pitch, dst_width,
                                                                   dst_height);

    return cudaGetLastError();
}

// RG depth to NV12 kernel - preserves both depth bytes
// R channel → Y plane (high byte)
// G channel → UV plane (low byte)
__global__ void rg_depth_to_nv12_scaled_kernel(cudaTextureObject_t tex, uint8_t* __restrict__ dst_nv12, size_t dst_pitch,
                                               uint32_t dst_width, uint32_t dst_height, uint32_t aligned_height) {
    uint32_t x = blockIdx.x * blockDim.x + threadIdx.x;
    uint32_t y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x >= dst_width || y >= dst_height)
        return;

    // Sample RG texture with normalized coordinates
    float  u     = (x + 0.5f) / dst_width;
    float  v     = (y + 0.5f) / dst_height;
    float4 pixel = tex2D<float4>(tex, u, v);

    // For RG depth: pixel = (R, G, 0, 0) or (R, G, 0, 1)
    // R = high byte of 16-bit depth
    // G = low byte of 16-bit depth

    // Store R channel (high byte) in Y plane
    uint8_t high_byte           = (uint8_t) (pixel.x * 255.0f);
    dst_nv12[y * dst_pitch + x] = high_byte;

    // Store G channel (low byte) in UV plane
    // Use 2x2 chroma subsampling like standard NV12
    if ((x & 1) == 0 && (y & 1) == 0) {
        uint32_t uv_y      = y / 2;
        uint32_t uv_offset = aligned_height * dst_pitch + uv_y * dst_pitch + x;

        uint8_t low_byte = (uint8_t) (pixel.y * 255.0f);

        // Store G in both U and V channels for reconstruction
        dst_nv12[uv_offset]     = low_byte; // U channel
        dst_nv12[uv_offset + 1] = low_byte; // V channel
    }
}

cudaError_t launch_rg_depth_to_nv12_scaled(cudaTextureObject_t tex, uint8_t* dst_nv12, size_t dst_pitch, uint32_t dst_width,
                                           uint32_t dst_height, uint32_t aligned_height, cudaStream_t stream) {
    dim3 block(16, 16);
    dim3 grid((dst_width + 15) / 16, (dst_height + 15) / 16);

    rg_depth_to_nv12_scaled_kernel<<<grid, block, 0, stream>>>(tex, dst_nv12, dst_pitch, dst_width, dst_height, aligned_height);

    return cudaGetLastError();
}

// ---------------------------------------------------------------------------
// RGBA16F motion vector to NV12 kernel
//
// Input:  RGBA16F texture where R=Vx, G=Vy, B=Vz, A=unused (always 1.0).
//         Velocities are world-space metres per frame.
// Output: NV12 buffer.
//   Y  plane (full resolution):       quantised Vx
//   UV plane (2x2 chroma-subsampled): U = quantised Vy, V = quantised Vz
//
// Normalisation maps [-MV_MAX_VEL, +MV_MAX_VEL] linearly to [0, 255].
// The decoder reverses: val = (byte / 255.0 - 0.5) * 2 * MV_MAX_VEL
//
// Default range ±2 m/frame covers typical head-motion at 72 Hz.
// Values outside the range are clamped (they encode as 0 or 255).
// ---------------------------------------------------------------------------

// Maximum velocity magnitude encoded per channel (metres per frame).
#define MV_MAX_VEL 2.0f

__device__ __forceinline__ uint8_t quantise_velocity(float v) {
    // Map [-MV_MAX_VEL, +MV_MAX_VEL] → [0, 255]
    float normalised = (v / MV_MAX_VEL) * 0.5f + 0.5f; // [0, 1]
    int   quantised  = __float2int_rn(normalised * 255.0f);
    return (uint8_t) device_clamp(quantised, 0, 255);
}

/// @brief Encode RGBA16F motion vectors into NV12 with optional downscale.
///
/// The texture object must be created with normalizedCoords=1 and bilinear
/// filtering (cudaFilterModeLinear) so that the kernel can downscale from
/// the source Vulkan image size to dst_width x dst_height in one pass.
__global__ void rgba16f_to_nv12_scaled_kernel(cudaTextureObject_t tex, uint8_t* __restrict__ dst_nv12, size_t dst_pitch,
                                              uint32_t dst_width, uint32_t dst_height, uint32_t aligned_height) {
    const uint32_t x = blockIdx.x * blockDim.x + threadIdx.x;
    const uint32_t y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x >= dst_width || y >= dst_height) {
        return;
    }

    // Sample with normalised coordinates so downscaling is free.
    const float  u       = (x + 0.5f) / static_cast<float>(dst_width);
    const float  v_coord = (y + 0.5f) / static_cast<float>(dst_height);
    const float4 pixel   = tex2D<float4>(tex, u, v_coord);

    // Y plane: quantised Vx (full resolution).
    dst_nv12[y * dst_pitch + x] = quantise_velocity(pixel.x);

    // UV plane: quantised Vy (U) and Vz (V) at half resolution.
    if ((x & 1) == 0 && (y & 1) == 0) {
        const uint32_t uv_row    = y / 2;
        const uint32_t uv_offset = aligned_height * dst_pitch + uv_row * dst_pitch + x;
        dst_nv12[uv_offset]      = quantise_velocity(pixel.y); // U = Vy
        dst_nv12[uv_offset + 1]  = quantise_velocity(pixel.z); // V = Vz
    }
}

/// @brief Launch motion-vector RGBA16F → NV12 conversion from a texture.
///
/// @param tex          CUDA texture object (RGBA16F, normalised coords, linear filter).
/// @param dst_nv12     Output NV12 buffer on device.
/// @param dst_pitch    Row pitch of dst_nv12 in bytes (>= dst_width).
/// @param dst_width    Output width in pixels (must be even).
/// @param dst_height   Output height in pixels (must be even).
/// @param aligned_height  NVENC-aligned height used to locate the UV plane.
/// @param stream       CUDA stream.
cudaError_t launch_rgba16f_to_nv12_scaled(cudaTextureObject_t tex, uint8_t* dst_nv12, size_t dst_pitch, uint32_t dst_width,
                                          uint32_t dst_height, uint32_t aligned_height, cudaStream_t stream) {
    dim3 block(16, 16);
    dim3 grid((dst_width + 15) / 16, (dst_height + 15) / 16);

    rgba16f_to_nv12_scaled_kernel<<<grid, block, 0, stream>>>(tex, dst_nv12, dst_pitch, dst_width, dst_height, aligned_height);

    return cudaGetLastError();
}

// ---------------------------------------------------------------------------
// RGBA16F → P010  (10-bit YUV 4:2:0, MSB-packed uint16_t)
//
// P010 layout (same as NV12 but each sample is uint16_t):
//   Y  plane: aligned_height rows × pitch bytes, one uint16_t per pixel
//   UV plane: (aligned_height/2) rows × pitch bytes, interleaved U/V uint16_t pairs
//
// Encoding convention:
//   Y  ← quantised Vx  (full resolution)
//   U  ← quantised Vy  (half resolution, 2×2 block average implicit via bilinear tex)
//   V  ← quantised Vz  (half resolution)
//
// 10-bit quantisation: maps [-MV_MAX_VEL, +MV_MAX_VEL] → [0, 1023].
// Each uint16_t stores the 10-bit value in the 10 MSBs (bits [15:6]); the
// 6 LSBs are 0.  This is the standard P010 packing used by NVENC and most
// hardware decoders.
//
// Decoder reversal:
//   val = ((uint16_t >> 6) / 1023.0 - 0.5) * 2 * MV_MAX_VEL
// ---------------------------------------------------------------------------

__device__ __forceinline__ uint16_t quantise_velocity_10bit(float v) {
    // Map [-MV_MAX_VEL, +MV_MAX_VEL] → [0, 1023]
    float normalised = (v / MV_MAX_VEL) * 0.5f + 0.5f; // → [0, 1]
    int   quantised  = __float2int_rn(normalised * 1023.0f);
    quantised        = device_clamp(quantised, 0, 1023);
    return static_cast<uint16_t>(quantised << 6); // P010: value in MSBs
}

/// @brief Encode RGBA16F motion vectors into P010 with optional downscale.
///
/// @param tex            CUDA texture object (RGBA16F source).
///                       Must be created with normalizedCoords=1, linear filter,
///                       and readMode=cudaReadModeElementType.
/// @param dst_p010       Output P010 buffer on device (uint16_t elements).
/// @param dst_pitch_bytes Row pitch of dst_p010 in BYTES (>= dst_width * 2).
/// @param dst_width      Output width in pixels (must be even).
/// @param dst_height     Output height in pixels (must be even).
/// @param aligned_height NVENC-aligned height used to locate the UV plane.
__global__ void rgba16f_to_p010_scaled_kernel(cudaTextureObject_t tex, uint16_t* __restrict__ dst_p010, size_t dst_pitch_bytes,
                                              uint32_t dst_width, uint32_t dst_height, uint32_t aligned_height) {
    const uint32_t x = blockIdx.x * blockDim.x + threadIdx.x;
    const uint32_t y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x >= dst_width || y >= dst_height) {
        return;
    }

    // Normalised sample coordinates (bilinear downscale is free via the texture unit).
    const float  u       = (x + 0.5f) / static_cast<float>(dst_width);
    const float  v_coord = (y + 0.5f) / static_cast<float>(dst_height);
    const float4 pixel   = tex2D<float4>(tex, u, v_coord);

    // P010 row stride in uint16_t elements.
    const size_t stride = dst_pitch_bytes / 2;

    // Y plane (full resolution): Vx
    dst_p010[y * stride + x] = quantise_velocity_10bit(pixel.x);

    // UV plane (half resolution): Vy → U, Vz → V.
    // One UV pair covers a 2×2 luma block; we write on even pixels of even rows.
    if ((x & 1) == 0 && (y & 1) == 0) {
        const uint32_t uv_row = y / 2;
        // UV plane starts at aligned_height * dst_pitch_bytes bytes from the base.
        const size_t uv_base    = (static_cast<size_t>(aligned_height) * dst_pitch_bytes) / 2;
        const size_t uv_offset  = uv_base + uv_row * stride + x;
        dst_p010[uv_offset]     = quantise_velocity_10bit(pixel.y); // U = Vy
        dst_p010[uv_offset + 1] = quantise_velocity_10bit(pixel.z); // V = Vz
    }
}

/// @brief Launch motion-vector RGBA16F → P010 conversion from a texture.
cudaError_t launch_rgba16f_to_p010_scaled(cudaTextureObject_t tex, uint16_t* dst_p010, size_t dst_pitch_bytes,
                                          uint32_t dst_width, uint32_t dst_height, uint32_t aligned_height,
                                          cudaStream_t stream) {
    dim3 block(16, 16);
    dim3 grid((dst_width + 15) / 16, (dst_height + 15) / 16);

    rgba16f_to_p010_scaled_kernel<<<grid, block, 0, stream>>>(tex, dst_p010, dst_pitch_bytes, dst_width, dst_height,
                                                              aligned_height);

    return cudaGetLastError();
}

// ---------------------------------------------------------------------------
// COMBINED_ENCODING: stereo BGRA → NV12
//
// Blits a left-eye texture and a right-eye texture side-by-side into a single
// NV12 buffer whose total width is (dst_eye_width * 2).  Each thread handles
// one output pixel and selects the correct source texture based on whether its
// absolute x-coordinate falls in the left or right half.
//
// Source textures must be created with normalizedCoords=1 and bilinear
// filtering (cudaFilterModeLinear) so that any source-to-target scale ratio
// (e.g. Monado render scale ≠ 100%) is resolved in the texture unit with no
// extra passes or temporary buffers.
//
// UV chroma (4:2:0) is computed from the 2×2 block within each eye's half.
// Because dst_eye_width is always even after HEVC alignment, the left/right
// boundary always falls on an even column and no cross-eye UV sampling occurs.
// ---------------------------------------------------------------------------

#ifdef COMBINED_ENCODING

__global__ void bgra_stereo_to_nv12_kernel(cudaTextureObject_t left_tex, cudaTextureObject_t right_tex,
                                           uint8_t* __restrict__ y_dst, uint8_t* __restrict__ uv_dst, size_t dst_pitch,
                                           uint32_t dst_eye_width, uint32_t dst_height) {
    // x spans the full combined width [0, dst_eye_width * 2).
    const uint32_t x = blockIdx.x * blockDim.x + threadIdx.x;
    const uint32_t y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x >= dst_eye_width * 2 || y >= dst_height) {
        return;
    }

    // Select the appropriate eye texture and compute the normalized UV
    // coordinate within that eye's half of the output.
    const bool     is_right = (x >= dst_eye_width);
    const uint32_t local_x  = is_right ? (x - dst_eye_width) : x;

    const float inv_w = 1.0f / static_cast<float>(dst_eye_width);
    const float inv_h = 1.0f / static_cast<float>(dst_height);
    const float u     = (static_cast<float>(local_x) + 0.5f) * inv_w;
    const float v     = (static_cast<float>(y) + 0.5f) * inv_h;

    // Sample from the appropriate eye texture.
    // Channel mapping .x=R .y=G .z=B matches bgra_texture_to_nv12_scaled_kernel.
    float4 pixel = is_right ? tex2D<float4>(right_tex, u, v) : tex2D<float4>(left_tex, u, v);

    const int r = static_cast<int>(pixel.x * 255.0f + 0.5f);
    const int g = static_cast<int>(pixel.y * 255.0f + 0.5f);
    const int b = static_cast<int>(pixel.z * 255.0f + 0.5f);

    // Write Y plane (full resolution).
    int y_val                = ((Y_R * r + Y_G * g + Y_B * b + 128) >> 8);
    y_val                    = device_clamp(y_val, 0, 255);
    y_dst[y * dst_pitch + x] = static_cast<uint8_t>(y_val);

    // Write UV plane (2×2 chroma subsampled).
    // Only threads at even (x, y) write UV.  The 2×2 block is sampled entirely
    // within the same eye's half so no cross-eye UV bleeding occurs.
    if ((x & 1) == 0 && (y & 1) == 0) {
        int r_sum = r, g_sum = g, b_sum = b;
        int valid = 1;

        // Right neighbor — stays in the same eye half (dst_eye_width is even).
        if (local_x + 1 < dst_eye_width) {
            const float u1 = (static_cast<float>(local_x) + 1.5f) * inv_w;
            float4      p1 = is_right ? tex2D<float4>(right_tex, u1, v) : tex2D<float4>(left_tex, u1, v);
            r_sum += static_cast<int>(p1.x * 255.0f + 0.5f);
            g_sum += static_cast<int>(p1.y * 255.0f + 0.5f);
            b_sum += static_cast<int>(p1.z * 255.0f + 0.5f);
            valid++;
        }
        // Bottom neighbor.
        if (y + 1 < dst_height) {
            const float v1 = (static_cast<float>(y) + 1.5f) * inv_h;
            float4      p2 = is_right ? tex2D<float4>(right_tex, u, v1) : tex2D<float4>(left_tex, u, v1);
            r_sum += static_cast<int>(p2.x * 255.0f + 0.5f);
            g_sum += static_cast<int>(p2.y * 255.0f + 0.5f);
            b_sum += static_cast<int>(p2.z * 255.0f + 0.5f);
            valid++;
        }
        // Bottom-right neighbor.
        if (local_x + 1 < dst_eye_width && y + 1 < dst_height) {
            const float u1 = (static_cast<float>(local_x) + 1.5f) * inv_w;
            const float v1 = (static_cast<float>(y) + 1.5f) * inv_h;
            float4      p3 = is_right ? tex2D<float4>(right_tex, u1, v1) : tex2D<float4>(left_tex, u1, v1);
            r_sum += static_cast<int>(p3.x * 255.0f + 0.5f);
            g_sum += static_cast<int>(p3.y * 255.0f + 0.5f);
            b_sum += static_cast<int>(p3.z * 255.0f + 0.5f);
            valid++;
        }

        const int r_avg = r_sum / valid;
        const int g_avg = g_sum / valid;
        const int b_avg = b_sum / valid;

        int u_val = ((U_R * r_avg + U_G * g_avg + U_B * b_avg + 128) >> 8) + 128;
        int v_val = ((V_R * r_avg + V_G * g_avg + V_B * b_avg + 128) >> 8) + 128;
        u_val     = device_clamp(u_val, 0, 255);
        v_val     = device_clamp(v_val, 0, 255);

        // UV x is the absolute half-resolution column spanning both eyes.
        const size_t uv_x   = x >> 1;
        const size_t uv_y   = y >> 1;
        const size_t uv_idx = uv_y * dst_pitch + uv_x * 2;
        uv_dst[uv_idx + 0]  = static_cast<uint8_t>(u_val);
        uv_dst[uv_idx + 1]  = static_cast<uint8_t>(v_val);
    }
}

cudaError_t launch_bgra_stereo_to_nv12(cudaTextureObject_t left_tex, cudaTextureObject_t right_tex, uint8_t* dst_nv12,
                                       size_t dst_pitch, uint32_t dst_eye_width, uint32_t dst_height, uint32_t aligned_height,
                                       cudaStream_t stream) {
    uint8_t* y_plane  = dst_nv12;
    uint8_t* uv_plane = dst_nv12 + dst_pitch * aligned_height;

    // Grid covers the full combined width (2 × dst_eye_width) × dst_height.
    dim3 block(16, 16);
    dim3 grid(((dst_eye_width * 2) + block.x - 1) / block.x, (dst_height + block.y - 1) / block.y);

    bgra_stereo_to_nv12_kernel<<<grid, block, 0, stream>>>(left_tex, right_tex, y_plane, uv_plane, dst_pitch, dst_eye_width,
                                                           dst_height);

    return cudaGetLastError();
}

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

#endif // COMBINED_ENCODING

} // extern "C"
