// NvencEncoder.hpp
#pragma once
#include <vector>
#include <unordered_map>
#include <cuda.h>
#include "NvEncoder/NvEncoderCuda.h"
#include "nvEncodeAPI.h"

struct NvencParams {
    int width;
    int height;
    int fps;
    int64_t bitrate;      // bps
    bool is_hevc;         // false = H.264, true = HEVC
};

class NvencEncoder {
public:
    NvencEncoder() = default;
    ~NvencEncoder();

    // Initialize with an existing CUDA context (same GPU as your frames)
    void init(CUcontext cu_ctx, const NvencParams& p);

    // Build and cache QP map (per frame) and encode one picture from a CUDA ptr/pitch.
    // Returns Annex-B bitstream (vector of bytes).
    std::vector<uint8_t> encode_with_qp_map(CUdeviceptr dev_ptr, uint32_t pitch,
                                            int64_t pts, // your timestamp or frame counter
                                            float gaze_x_px, float gaze_y_px,
                                            float fovea_r_px, float mid_r_px,
                                            int8_t dq_fovea, int8_t dq_mid, int8_t dq_periph);

    void reconfigure_bitrate(int64_t new_bps, bool force_idr);

private:
    void build_qp_map(int mbw, int mbh,
                      float gaze_x_px, float gaze_y_px,
                      float r0_px, float r1_px,
                      int8_t dq0, int8_t dq1, int8_t dq2);

    CUcontext cu_ctx_ = nullptr;
    NvEncoderCuda* enc_ = nullptr;
    NV_ENC_INITIALIZE_PARAMS init_{};
    NV_ENC_CONFIG cfg_{};
    int W_ = 0, H_ = 0, fps_ = 0;
    bool hevc_ = false;

    // Register-once cache (device pointer -> registered resource)
    std::unordered_map<CUdeviceptr, NV_ENC_REGISTERED_PTR> reg_cache_;
    std::vector<int8_t> qp_map_;
    int mb_w_ = 16; // H.264 MB default
    int mb_h_ = 16;

    int frame_count_ = 0;
};
