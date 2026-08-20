#include "nvdec_decoder.hpp"

#include <NvCodecUtils.h>
#include <spdlog/spdlog.h>

simplelogger::Logger* logger = simplelogger::LoggerFactory::CreateConsoleLogger(TRACE);

using namespace ILLIXR;

nvdec_decoder::nvdec_decoder(cudaVideoCodec codec)
    : codec_{codec} {
    // Initialize CUDA driver API
    if (cuInit(0) != CUDA_SUCCESS)
        throw std::runtime_error("nvdec_decoder: cuInit failed");

    CUdevice device = 0;
    if (cuDeviceGet(&device, 0) != CUDA_SUCCESS)
        throw std::runtime_error("nvdec_decoder: cuDeviceGet failed");

    // cuCtxCreate changed signature in CUDA 13.0 — it was remapped to
    // cuCtxCreate_v4 which takes an extra CUctxCreateParams* parameter.
    // Use a compile-time version guard to call the correct form.
    // Passing nullptr for CUctxCreateParams creates a regular context
    // equivalent to the old 3-argument call.
#if CUDA_VERSION >= 13000
    if (cuCtxCreate_v4(&cuda_ctx_, nullptr, CU_CTX_SCHED_BLOCKING_SYNC, device) != CUDA_SUCCESS)
        throw std::runtime_error("nvdec_decoder: cuCtxCreate failed");
#else
    if (cuCtxCreate_v2(&cuda_ctx_, CU_CTX_SCHED_BLOCKING_SYNC, device) != CUDA_SUCCESS)
        throw std::runtime_error("nvdec_decoder: cuCtxCreate failed");
#endif

    // bLowLatency=false: more tolerant of SPS/PPS embedded within IDR frames
    // rather than as a separate preceding packet. The Snapdragon encoder
    // embeds SPS/PPS inside IDR frames in Annex B format rather than
    // delivering them as a separate config packet beforehand.
    // bUseDeviceFrame=true: GetFrame() returns a CUDA device pointer.
    // Required because launch_nv12_to_rgb() operates on device memory.
    // bLowLatency=false: tolerates SPS/PPS embedded within IDR frames
    // rather than requiring them as a separate preceding packet.
    decoder_ = std::make_unique<NvDecoder>(cuda_ctx_, true, codec_,
                                           /*bLowLatency=*/false);
}

nvdec_decoder::~nvdec_decoder() {
    free_pinned();
    decoder_.reset();
    if (cuda_ctx_)
        cuCtxDestroy(cuda_ctx_);
}

const uint8_t* nvdec_decoder::decode(const uint8_t* data, size_t size) {
    // Log first 4 bytes to verify annexb start code [00 00 00 01]
    // and the input size so we can confirm what the server receives.
    if (size >= 8) {
        // Log 8 bytes: distinguishes Annex B [00 00 00 01 nal_type ...]
        // from AVCC [len3 len2 len1 len0 nal_type ...].
        // In Annex B byte[4] is the NAL unit type:
        //   0x67 = SPS, 0x68 = PPS, 0x65 = IDR, 0x61 = non-IDR
        // If byte[4] looks like a length (e.g. 0x00) this is AVCC format.
        spdlog::get("illixr")->info(
            "[nvdec] decode input: size={}B bytes=[{:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X}]", size, data[0],
            data[1], data[2], data[3], data[4], data[5], data[6], data[7]);
    } else {
        spdlog::get("illixr")->warn("[nvdec] decode input too small: size={}B", size);
    }

    int n_decoded = decoder_->Decode(const_cast<uint8_t*>(data), static_cast<int>(size));

    // Log n_decoded before calling GetWidth()/GetHeight() — those assert
    // m_nWidth != 0 and must not be called until the decoder has parsed SPS/PPS.
    spdlog::get("illixr")->debug("[nvdec] Decode() returned n_decoded={}", n_decoded);

    if (n_decoded <= 0) {
        spdlog::get("illixr")->debug("[nvdec] no output: n_decoded={} (0=buffering, <0=error)", n_decoded);
        return nullptr;
    }

    // GetWidth()/GetHeight() are only safe after n_decoded > 0.
    const int w = decoder_->GetWidth();
    const int h = decoder_->GetHeight();

    spdlog::get("illixr")->debug("[nvdec] decoded: n_decoded={} width={} height={}", n_decoded, w, h);

    // NvDecoder may return multiple frames at once (n_decoded > 1) when
    // bLowLatency=false. Drain all but the last — we only return the most
    // recent frame since the caller stores one frame per call.
    for (int i = 0; i < n_decoded - 1; ++i)
        decoder_->GetFrame(); // discard older buffered frames

    uint8_t* nv12_ptr = decoder_->GetFrame();
    if (nv12_ptr == nullptr) {
        spdlog::get("illixr")->warn("[nvdec] GetFrame() returned nullptr despite n_decoded={}", n_decoded);
        return nullptr;
    }

    // Allocate pinned host buffer and device RGB buffer on first use
    // or if dimensions change.
    ensure_buffers(w, h);

    // Align pitch to 256 bytes regardless of width — NVDEC always allocates
    // frames with 256-byte row alignment. Using GetDeviceFramePitch() directly
    // is correct but computing it explicitly makes the UV offset unambiguous
    // for any input resolution, including those where width is not a multiple
    // of 256 (e.g. 1920, 640, 320).
    const int      pitch    = ((w + 255) / 256) * 256;
    const uint8_t* y_plane  = nv12_ptr;
    const uint8_t* uv_plane = nv12_ptr + pitch * h;

    spdlog::get("illixr")->debug("[nvdec] frame width={} height={} pitch={} (from decoder={})", w, h, pitch,
                                 decoder_->GetDeviceFramePitch());

    // Probe raw NV12 Y plane before conversion to confirm decoder output.
    {
        uint8_t probe[256] = {};
        cudaMemcpy(probe, y_plane, 256, cudaMemcpyDeviceToHost);
        int64_t y_sum = 0;
        for (int i = 0; i < 256; ++i)
            y_sum += probe[i];
        spdlog::get("illixr")->info("[nvdec] raw NV12 Y plane mean over first 256 bytes = {:.1f}",
                                    static_cast<double>(y_sum) / 256);
    }

    launch_nv12_to_rgb(y_plane, uv_plane, device_rgb_, w, h, pitch, pitch, stream_);

    // Copy GPU RGB -> pinned host buffer
    cudaMemcpyAsync(pinned_rgb_, device_rgb_, static_cast<size_t>(w * h * 3), cudaMemcpyDeviceToHost, stream_);

    // Synchronize so pinned_rgb_ is ready for the CPU to read
    cudaStreamSynchronize(stream_);

    // Check for kernel errors
    cudaError_t err = cudaGetLastError();
    if (err != cudaSuccess)
        spdlog::get("illixr")->error("[nvdec] CUDA kernel error: {}", cudaGetErrorString(err));

    // Probe pinned buffer after conversion
    {
        int64_t sum   = 0;
        int     count = std::min(w * h * 3, 3000);
        for (int i = 0; i < count; ++i)
            sum += pinned_rgb_[i];
        spdlog::get("illixr")->info("[nvdec] pinned_rgb mean over first {} bytes = {:.1f}", count,
                                    static_cast<double>(sum) / count);
    }
    width_  = w;
    height_ = h;
    return pinned_rgb_;
}

void nvdec_decoder::ensure_buffers(int w, int h) {
    if (pinned_rgb_ != nullptr && w == width_ && h == height_)
        return;

    free_pinned();

    const size_t rgb_bytes = static_cast<size_t>(w * h * 3);

    // Pinned host buffer — zero-copy accessible from both CPU and GPU
    if (cudaMallocHost(&pinned_rgb_, rgb_bytes) != cudaSuccess)
        throw std::runtime_error("nvdec_decoder: cudaMallocHost failed");

    // Device buffer for the GPU-side RGB intermediate
    if (cudaMalloc(&device_rgb_, rgb_bytes) != cudaSuccess) {
        cudaFreeHost(pinned_rgb_);
        pinned_rgb_ = nullptr;
        throw std::runtime_error("nvdec_decoder: cudaMalloc for device_rgb failed");
    }

    if (stream_ == nullptr)
        cudaStreamCreate(&stream_);

    width_  = w;
    height_ = h;
}

void nvdec_decoder::free_pinned() {
    if (pinned_rgb_ != nullptr) {
        cudaFreeHost(pinned_rgb_);
        pinned_rgb_ = nullptr;
    }
    if (device_rgb_ != nullptr) {
        cudaFree(device_rgb_);
        device_rgb_ = nullptr;
    }
    if (stream_ != nullptr) {
        cudaStreamDestroy(stream_);
        stream_ = nullptr;
    }
}
