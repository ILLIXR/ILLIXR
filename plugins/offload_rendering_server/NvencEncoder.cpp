// NvencEncoder.cpp
#include "NvencEncoder.hpp"
#include <stdexcept>
#include <cmath>
#include <cstring>
#include <iostream>

static inline void check(bool ok, const char* msg) {
    if (!ok) throw std::runtime_error(msg);
}

NvencEncoder::~NvencEncoder() {
    if (enc_) {
        std::vector<NvEncOutputFrame> tmp;
        enc_->EndEncode(tmp);
        delete enc_;
        enc_ = nullptr;
    }
}

void NvencEncoder::init(CUcontext cu_ctx, const NvencParams& p) {
    cu_ctx_ = cu_ctx;
    W_ = p.width; 
    H_ = p.height; 
    fps_ = p.fps; 
    hevc_ = p.is_hevc;

    // Choose MB/CTU size for QP map indexing.
    // H.264 = 16x16. HEVC most commonly 32x32 CTU for NVENC low-latency presets.
    if (hevc_) { mb_w_ = 32; mb_h_ = 32; } 
    else { mb_w_ = 16; mb_h_ = 16; }

    // Our Vulkan frames are in BGRA, byte-order.
    // NVENC represents format in word-order, so ARGB matches Vulkan BGRA on little-endian.
    enc_ = new NvEncoderCuda(cu_ctx_, W_, H_,
                             NV_ENC_BUFFER_FORMAT_ARGB); 

    // Fill defaults then override what we need.
    memset(&init_, 0, sizeof(init_));
    memset(&cfg_, 0, sizeof(cfg_));
    init_.version = NV_ENC_INITIALIZE_PARAMS_VER;
    cfg_.version  = NV_ENC_CONFIG_VER;
    init_.encodeConfig = &cfg_;

    NV_ENC_PRESET_CONFIG presetCfg = { NV_ENC_PRESET_CONFIG_VER, { NV_ENC_CONFIG_VER } };
    GUID codec = hevc_ ? NV_ENC_CODEC_HEVC_GUID : NV_ENC_CODEC_H264_GUID;
    GUID preset = NV_ENC_PRESET_P3_GUID; // Low-latency high-quality; choose as you like
    enc_->CreateDefaultEncoderParams(&init_, codec, preset, NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY);

    // Attach cfg so we can tweak RC and emphasis map
    init_.encodeConfig = &cfg_;
    init_.frameRateNum = fps_;
    init_.frameRateDen = 1;
    init_.encodeWidth  = W_;
    init_.encodeHeight = H_;
    init_.enablePTD = 1; // Picture Type Decision - enable the encoder to decide IDR/P/B frames

    // Rate control (e.g., CBR low-latency)
    cfg_.rcParams.rateControlMode = NV_ENC_PARAMS_RC_CBR;
    cfg_.rcParams.averageBitRate  = (uint32_t)std::max<int64_t>(p.bitrate, 100000);
    cfg_.rcParams.maxBitRate      = cfg_.rcParams.averageBitRate;
    // Video buffering verifier (VBV) settings
    // Controls the variability in encoded bitrate and how long before decoding can start
    cfg_.rcParams.vbvBufferSize   = cfg_.rcParams.averageBitRate / 8; // 1 second buffer (in bits)
    cfg_.rcParams.vbvInitialDelay = cfg_.rcParams.vbvBufferSize / 10; // start at 10% full for low latency

    // Ultra-low latency, no B-frames, short GOP
    cfg_.gopLength = 15;
    cfg_.frameIntervalP = 1;
    cfg_.rcParams.enableAQ = 0;             // **Must disable AQ when using QP/Delta map**
    cfg_.rcParams.enableTemporalAQ = 0;
    // cfg_.rcParams.qpMapMode = NV_ENC_QP_MAP_DELTA; // enable QP delta map - emphasis map is only supported for H264
    // cfg_.rcParams.constQP = {20,20,20};

    // for hevc
    cfg_.encodeCodecConfig.hevcConfig.repeatSPSPPS = 1;
    cfg_.encodeCodecConfig.hevcConfig.outputAUD    = 1;
    cfg_.encodeCodecConfig.hevcConfig.idrPeriod    = cfg_.gopLength;
    cfg_.rcParams.enableLookahead = 0;

    std::cout << "NvencEncoder: initializing " << (hevc_ ? "HEVC" : "H.264")
              << " encoder " << W_ << "x" << H_ << " @ " << fps_
              << " fps, bitrate " << cfg_.rcParams.averageBitRate << " bps"
              << ", mb_size " << mb_w_ << "x" << mb_h_ << std::endl;
    enc_->CreateEncoder(&init_);
}

void NvencEncoder::build_qp_map(int mbw, int mbh,
                                float gx, float gy,
                                float r0, float r1,
                                int8_t dq0, int8_t dq1, int8_t dq2) {
    int cols = (W_ + mbw - 1)/mbw;
    int rows = (H_ + mbh - 1)/mbh;
    qp_map_.resize(cols * rows);

    for (int r = 0; r < rows; ++r) {
        float cy = (r + 0.5f) * mbh;
        for (int c = 0; c < cols; ++c) {
            float cx = (c + 0.5f) * mbw;
            float dx = cx - gx, dy = cy - gy;
            float d  = std::sqrt(dx*dx + dy*dy);
            int8_t dq = (d <= r0) ? dq0 : (d <= r1 ? dq1 : dq2);
            qp_map_[r*cols + c] = dq;    // negative = more quality
        }
    }
}

std::vector<uint8_t> NvencEncoder::encode_with_qp_map(CUdeviceptr dev_ptr, uint32_t pitch,
                                                      int64_t pts,
                                                      float gaze_x_px, float gaze_y_px,
                                                      float fovea_r_px, float mid_r_px,
                                                      int8_t dq_fovea, int8_t dq_mid, int8_t dq_periph) {
    // Copy the frame to the device
    // TODO: avoid the copy
    const NvEncInputFrame* in = enc_->GetNextInputFrame();

    NvEncoderCuda::CopyToDeviceFrame(
        cu_ctx_,
        /*pSrcFrame*/   (void*)dev_ptr,          // your CUdeviceptr from AVFrame
        /*nSrcPitch*/   pitch,                   // bytes per row
        /*dstDevPtr*/   (CUdeviceptr)in->inputPtr,
        /*dstPitch*/    in->pitch,
        /*width*/       W_, 
        /*height*/      H_,
        /*srcMemType*/  CU_MEMORYTYPE_DEVICE,
        // /*bufferFormat*/in->bufferFormat,        // must match your init (e.g., ABGR)
        NV_ENC_BUFFER_FORMAT_ARGB,
        // /*chromaOffsets*/in->chromaOffsets,
        // /*numChromaPlanes*/in->numChromaPlanes
        0, 0
    );

    build_qp_map(mb_w_, mb_h_, gaze_x_px, gaze_y_px, fovea_r_px, mid_r_px,
                 dq_fovea, dq_mid, dq_periph);

    NV_ENC_PIC_PARAMS pic = { NV_ENC_PIC_PARAMS_VER };
    pic.inputBuffer     = in->inputPtr;
    pic.inputPitch      = in->pitch;
    // pic.bufferFmt       = in->bufferFormat;
    pic.bufferFmt       = NV_ENC_BUFFER_FORMAT_ARGB;
    pic.inputWidth      = W_;
    pic.inputHeight     = H_;
    pic.pictureStruct   = NV_ENC_PIC_STRUCT_FRAME;
    pic.inputTimeStamp  = (uint64_t)pts;
    // pic.qpDeltaMap      = (int8_t*)qp_map_.data();
    // pic.qpDeltaMapSize  = (uint32_t)qp_map_.size();
    // pic.encodePicFlags  = (frame_count_++ == 0) ? NV_ENC_PIC_FLAG_FORCEIDR : 0; // set NV_ENC_PIC_FLAG_FORCEIDR if you need an IDR
    pic.encodePicFlags = NV_ENC_PIC_FLAG_FORCEIDR;

    // Optional drain of pending frames
    // std::vector<NvEncOutputFrame> drain;
    // enc_->EncodeFrame(drain);

    // Encode the current frame
    std::vector<NvEncOutputFrame> vPackets;
    enc_->EncodeFrame(vPackets, &pic);

    // Concatenate encoded bitstreams (Annex-B NALUs)
    std::vector<uint8_t> out;
    size_t total = 0;
    for (auto& pkt : vPackets)
        total += pkt.frame.size();   // each NvEncOutputFrame holds a bitstream

    out.reserve(total);
    for (auto& pkt : vPackets)
        out.insert(out.end(), pkt.frame.begin(), pkt.frame.end());

    // Now `out` contains contiguous NALUs suitable for sending or wrapping in AVPacket
    std::cout << "NvencEncoder: encoded frame size = " << out.size() << " bytes" << std::endl;
    return out;
}

void NvencEncoder::reconfigure_bitrate(int64_t new_bps, bool force_idr) {
    NV_ENC_RECONFIGURE_PARAMS rp = { NV_ENC_RECONFIGURE_PARAMS_VER };
    rp.reInitEncodeParams = init_;
    rp.reInitEncodeParams.encodeConfig = &cfg_;
    cfg_.rcParams.averageBitRate = (uint32_t)new_bps;
    cfg_.rcParams.maxBitRate     = (uint32_t)new_bps;
    cfg_.rcParams.vbvBufferSize  = (uint32_t)(new_bps / std::max(1, fps_));
    rp.forceIDR = force_idr ? 1 : 0;
    enc_->Reconfigure(&rp);
}
