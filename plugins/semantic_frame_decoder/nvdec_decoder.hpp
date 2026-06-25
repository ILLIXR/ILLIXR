#pragma once

/*!
 * @file nvdec_decoder.hpp
 * @brief NVDEC hardware H.265 decoder for the semantic_python plugin.
 *
 * Wraps the NVIDIA Video Codec SDK NvDecoder sample class. Decoded NV12
 * frames are converted to packed RGB uint8 on the GPU via nv12_to_rgb.cu,
 * then copied to a pre-allocated pinned host buffer. The pinned buffer is
 * exposed to Python as a zero-copy numpy array via a pybind11 capsule.
 *
 * Output: shape (height, width, 3), dtype uint8, channel order RGB.
 *
 * Supports SDK 12.x and 13.x. AV1 decode is conditionally compiled when
 * NVDEC_HAS_AV1 is defined (requires Ada Lovelace or newer GPU).
 *
 * Thread safety: not thread-safe. All calls must come from the same thread
 * (the switchboard decode callback thread).
 */

#include "nv12_to_rgb.cuh"

#include <cstdint>
#include <cuda.h>
#include <cuda_runtime.h>
#include <NvDecoder/NvDecoder.h>
#include <stdexcept>
#include <string>

namespace ILLIXR {

class nvdec_decoder {
public:
    explicit nvdec_decoder(cudaVideoCodec codec = cudaVideoCodec_HEVC);

    ~nvdec_decoder();

    // Non-copyable, non-movable - owns CUDA context
    nvdec_decoder(const nvdec_decoder&)            = delete;
    nvdec_decoder& operator=(const nvdec_decoder&) = delete;

    /**
     * @brief Decode one H.265 annexb access unit.
     *
     * Decodes the input bytes and converts the resulting NV12 frame to
     * packed RGB uint8 on the GPU, then copies to a pre-allocated pinned
     * host buffer.
     *
     * @param data     Pointer to annexb bytes.
     * @param size     Byte count.
     * @return         Pointer to pinned host RGB buffer (H x W x 3, uint8,
     *                 RGB order), or nullptr if no frame was output this
     *                 call (e.g. during decoder init with SPS/PPS only).
     *                 The pointer is valid until the next call to decode().
     */
    const uint8_t* decode(const uint8_t* data, size_t size, const int out_width, const int out_height);

    int width() const {
        return width_;
    }

    int height() const {
        return height_;
    }

private:
    void ensure_buffers(int w, int h);

    void free_pinned();

    cudaVideoCodec             codec_;
    CUcontext                  cuda_ctx_ = nullptr;
    std::unique_ptr<NvDecoder> decoder_;

    int      device_width_  = 0;
    int      device_height_ = 0;
    uint8_t*     pinned_rgb_ = nullptr; // pinned host buffer, H*W*3 bytes
    uint8_t*     device_rgb_ = nullptr; // device buffer for GPU RGB conversion
    cudaStream_t stream_     = nullptr;
    int          width_      = 0;
    int          height_     = 0;
};

} // namespace ILLIXR
