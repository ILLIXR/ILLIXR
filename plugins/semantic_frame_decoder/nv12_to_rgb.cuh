#pragma once

#include <cuda_runtime.h>
#include <cstdint>

/*!
 * @brief Launch the NV12 -> packed RGB uint8 conversion kernel.
 *
 * @param y_plane    Device pointer to Y plane (height x y_pitch bytes)
 * @param uv_plane   Device pointer to interleaved UV plane (height/2 x uv_pitch bytes)
 * @param rgb_out    Pinned host pointer (or device pointer) for output RGB
 *                   (height x width x 3 bytes, packed, no padding)
 * @param width      Frame width in pixels
 * @param height     Frame height in pixels
 * @param y_pitch    Y plane row stride in bytes
 * @param uv_pitch   UV plane row stride in bytes
 * @param stream     CUDA stream (0 for default)
 */
void launch_nv12_to_rgb(
    const uint8_t* y_plane,
    const uint8_t* uv_plane,
    uint8_t*       rgb_out,
    int            width,
    int            height,
    int            y_pitch,
    int            uv_pitch,
    cudaStream_t   stream = 0);
