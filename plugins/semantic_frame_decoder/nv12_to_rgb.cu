/*!
 * @file nv12_to_rgb.cu
 * @brief CUDA kernel converting NV12 (YUV 4:2:0 semi-planar) device memory
 *        to packed RGB uint8 host-pinned memory.
 *
 * NV12 layout:
 *   Y plane:  [height rows x width cols], 1 byte per pixel
 *   UV plane: [height/2 rows x width cols], interleaved Cb/Cr, 2 bytes per pair
 *
 * Output: packed RGB, 3 bytes per pixel, row-major, shape (height, width, 3).
 *
 * BT.601 full-range coefficients (Quest 3 Camera2 HAL delivers full-range YUV):
 *   R = clamp(Y              + 1.402*(Cr-128), 0, 255)
 *   G = clamp(Y - 0.344*(Cb-128) - 0.714*(Cr-128), 0, 255)
 *   B = clamp(Y + 1.772*(Cb-128)              , 0, 255)
 */

#include "nv12_to_rgb.cuh"

#include <cuda_runtime.h>
#include <cstdint>

// ---------------------------------------------------------------------------
// Kernel
// ---------------------------------------------------------------------------

__global__ void nv12_to_rgb_kernel(
    const uint8_t* __restrict__ y_plane,
    const uint8_t* __restrict__ uv_plane,
    uint8_t*       __restrict__ rgb_out,
    int width,
    int height,
    int y_pitch,
    int uv_pitch)
{
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x >= width || y >= height)
        return;

    // Y sample — full-range, no offset needed
    const float Y  = static_cast<float>(y_plane[y * y_pitch + x]);

    // UV sample — each UV pair covers a 2x2 block of Y pixels
    const int   uv_row = (y / 2) * uv_pitch;
    const int   uv_col = (x / 2) * 2;
    const float Cb = static_cast<float>(uv_plane[uv_row + uv_col])     - 128.f;
    const float Cr = static_cast<float>(uv_plane[uv_row + uv_col + 1]) - 128.f;

    // BT.601 full-range conversion
    const float R  = Y + 1.402f * Cr;
    const float G  = Y - 0.344f * Cb - 0.714f * Cr;
    const float B  = Y + 1.772f * Cb;

    // Clamp and pack RGB
    const int out_idx    = (y * width + x) * 3;
    rgb_out[out_idx + 0] = static_cast<uint8_t>(__float2int_rn(fminf(fmaxf(R, 0.f), 255.f)));
    rgb_out[out_idx + 1] = static_cast<uint8_t>(__float2int_rn(fminf(fmaxf(G, 0.f), 255.f)));
    rgb_out[out_idx + 2] = static_cast<uint8_t>(__float2int_rn(fminf(fmaxf(B, 0.f), 255.f)));
}

// ---------------------------------------------------------------------------
// Host-callable wrapper
// ---------------------------------------------------------------------------

void launch_nv12_to_rgb(
    const uint8_t* y_plane,
    const uint8_t* uv_plane,
    uint8_t*       rgb_out,
    int            width,
    int            height,
    int            y_pitch,
    int            uv_pitch,
    cudaStream_t   stream)
{
    const dim3 block(16, 16);
    const dim3 grid((width  + block.x - 1) / block.x,
                    (height + block.y - 1) / block.y);

    nv12_to_rgb_kernel<<<grid, block, 0, stream>>>(
        y_plane, uv_plane, rgb_out, width, height, y_pitch, uv_pitch);
}
