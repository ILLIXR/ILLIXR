#include "nvenc_encoder.hpp"

#include <cuda_runtime.h>
#include <future>
#include <nvEncodeAPI.h>
#include <opencv2/opencv.hpp>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <vector>

using namespace ILLIXR;

void nvenc_encoder::check_nvenc(NVENCSTATUS status, const char* msg) {
    if (status != NV_ENC_SUCCESS) {
        spdlog::get("illixr")->error(std::string(msg) + " Error: " + std::to_string(status));
        throw std::runtime_error(std::string(msg) + " Error: " + std::to_string(status));
    }
}

void nvenc_encoder::check_cuda(CUresult result, const char* msg) {
    if (result != CUDA_SUCCESS) {
        spdlog::get("illixr")->error(std::string(msg) + " Error: " + std::to_string(result));
        throw std::runtime_error(std::string(msg) + " Error: " + std::to_string(result));
    }
}


// Load NVENC API functions
typedef NVENCSTATUS (NVENCAPI* PNVENCODEAPICREATEINSTANCE)(NV_ENCODE_API_FUNCTION_LIST*);


[[maybe_unused]] nvenc_encoder::nvenc_encoder(int w, int h, int bitrate)
    : width_{w}
    , height_{h}
    , bitrate_{bitrate} {
    // Align dimensions to 32 for HEVC (more strict than H.264)
    aligned_width_ = (w + 31) & ~31;
    aligned_height_ = (h + 31) & ~31;

    spdlog::get("illixr")->info("HEVC encoder: input {}x{}, aligned {}x{}",
                                width_, height_, aligned_width_, aligned_height_);

    init_cuda();
    init_nvenc();
    query_capabilities();  // Check what's actually supported
    init_encoder();
    create_buffers();
        
    spdlog::get("illixr")->info("HEVC encoder initialized: {}x{} @ {} bps", 
                                width_, height_, bitrate_);
}

void nvenc_encoder::init_cuda() {
    // Initialize CUDA
    check_cuda(cuInit(0), "CUDA init failed");

    CUdevice cu_device;
    check_cuda(cuDeviceGet(&cu_device, 0), "Get CUDA device failed");
    char device_name[256];
    check_cuda(cuDeviceGetName(device_name, sizeof(device_name), cu_device), "");
    spdlog::get("illixr")->debug("GPU in use: {}", device_name);
    check_cuda(cuCtxCreate(&cu_context_, 0, cu_device), "Create CUDA context failed");
}

void nvenc_encoder::init_nvenc() {
    // Load NVENC library
    printf("NVENC API version: %d.%d\n", NVENCAPI_MAJOR_VERSION, NVENCAPI_MINOR_VERSION);
    CUcontext current;
    cuCtxGetCurrent(&current);
    printf("CUDA context: %p\n", current);
    #ifdef _WIN32
    HMODULE nvenc_lib = LoadLibrary(TEXT("nvEncodeAPI64.dll"));
    #else
    void* nvenc_lib = dlopen("libnvidia-encode.so.1", RTLD_LAZY);
    #endif
    if (!nvenc_lib) {
        throw std::runtime_error("Failed to load NVENC library");
    }

    auto create_instance = (PNVENCODEAPICREATEINSTANCE)
#ifdef _WIN32
        GetProcAddress(nvenc_lib, "NvEncodeAPICreateInstance");
    #else
    dlsym(nvenc_lib, "NvEncodeAPICreateInstance");
    #endif

    if (!create_instance) {
        throw std::runtime_error("Failed to get NvEncodeAPICreateInstance");
    }
    memset(&nvenc_, 0, sizeof(NV_ENCODE_API_FUNCTION_LIST));
    nvenc_.version = NV_ENCODE_API_FUNCTION_LIST_VER;
    check_nvenc(create_instance(&nvenc_), "Create NVENC instance failed");

    if (!nvenc_.nvEncOpenEncodeSessionEx) {
        cuCtxDestroy(current);
        throw std::runtime_error("nvEncOpenEncodeSessionEx not available");
    }

    // Open encode session
    NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS session_params = {};
    memset(&session_params, 0, sizeof(NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS));
    session_params.version    = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
    session_params.device     = cu_context_;
    session_params.deviceType = NV_ENC_DEVICE_TYPE_CUDA;
    session_params.apiVersion = NVENCAPI_VERSION;

    check_nvenc(nvenc_.nvEncOpenEncodeSessionEx(&session_params, &encoder_), "Open encode session failed");
}

void nvenc_encoder::query_capabilities() {
    // Query supported codecs
    uint32_t codec_count = 0;
    nvenc_.nvEncGetEncodeGUIDCount(encoder_, &codec_count);

    std::vector<GUID> codecs(codec_count);
    nvenc_.nvEncGetEncodeGUIDs(encoder_, codecs.data(), codec_count, &codec_count);

    bool hevc_supported = false;
    for (const auto& guid : codecs) {
        if (memcmp(&guid, &NV_ENC_CODEC_HEVC_GUID, sizeof(GUID)) == 0) {
            hevc_supported = true;
            spdlog::get("illixr")->info("HEVC encoding supported");
            break;
        }
    }

    if (!hevc_supported) {
        throw std::runtime_error("HEVC encoding not supported on this GPU");
    }

    // Query supported presets for HEVC
    uint32_t preset_count = 0;
    nvenc_.nvEncGetEncodePresetCount(encoder_, NV_ENC_CODEC_HEVC_GUID, &preset_count);
    spdlog::get("illixr")->info("HEVC preset count: {}", preset_count);

    // Query supported profiles for HEVC
    uint32_t profile_count = 0;
    nvenc_.nvEncGetEncodeProfileGUIDCount(encoder_, NV_ENC_CODEC_HEVC_GUID, &profile_count);
    spdlog::get("illixr")->info("HEVC profile count: {}", profile_count);

    std::vector<GUID> profiles(profile_count);
    nvenc_.nvEncGetEncodeProfileGUIDs(encoder_, NV_ENC_CODEC_HEVC_GUID, profiles.data(), profile_count, &profile_count);

    for (uint32_t i = 0; i < profile_count; i++) {
        const char* name = "Unknown";
        if (memcmp(&profiles[i], &NV_ENC_HEVC_PROFILE_MAIN_GUID, sizeof(GUID)) == 0) {
            name = "Main";
        } else if (memcmp(&profiles[i], &NV_ENC_HEVC_PROFILE_MAIN10_GUID, sizeof(GUID)) == 0) {
            name = "Main10";
        } else if (memcmp(&profiles[i], &NV_ENC_HEVC_PROFILE_FREXT_GUID, sizeof(GUID)) == 0) {
            name = "FREXT";
        }
        spdlog::get("illixr")->info("Profile {}: {}", i, name);
    }
}

void nvenc_encoder::init_encoder() {
    // Get preset config as base
    NV_ENC_PRESET_CONFIG preset_config = {};
    memset(&preset_config, 0, sizeof(NV_ENC_PRESET_CONFIG));
    preset_config.version = NV_ENC_PRESET_CONFIG_VER;
    preset_config.presetCfg.version = NV_ENC_CONFIG_VER;

    check_nvenc(nvenc_.nvEncGetEncodePresetConfigEx(encoder_, NV_ENC_CODEC_HEVC_GUID, NV_ENC_PRESET_P4_GUID,
                                                    NV_ENC_TUNING_INFO_LOW_LATENCY,  // Required for Ex version
                                                    &preset_config),"Failed to get preset: {}");

    // Copy preset as base
    NV_ENC_CONFIG encode_config = {};
    memcpy(&encode_config, &preset_config.presetCfg, sizeof(NV_ENC_CONFIG));

    encode_config.version = NV_ENC_CONFIG_VER;
    // HEVC Main profile (8-bit, 4:2:0) - Quest 3 compatible
    encode_config.profileGUID = NV_ENC_HEVC_PROFILE_MAIN_GUID;

    encode_config.encodeCodecConfig.hevcConfig.chromaFormatIDC = 1;  // 4:2:0
    encode_config.encodeCodecConfig.hevcConfig.repeatSPSPPS = 1;  // Include headers with each IDR
    //encode_config.encodeCodecConfig.hevcConfig.outputAUD = 1;

    // Disable features that might cause issues
    encode_config.encodeCodecConfig.hevcConfig.enableAlphaLayerEncoding = 0;

    // Rate control
    encode_config.rcParams.rateControlMode = NV_ENC_PARAMS_RC_CBR;
    encode_config.rcParams.averageBitRate = bitrate_;
    encode_config.rcParams.maxBitRate = bitrate_;
    //encode_config.rcParams.vbvBufferSize = bitrate_ / 30;  // ~1 frame

    // GOP settings
    encode_config.gopLength = 30;
    encode_config.frameIntervalP = 1;  // No B-frames for low latency

    // Initialize encoder
    NV_ENC_INITIALIZE_PARAMS init_params = {};
    memset(&init_params, 0, sizeof(NV_ENC_INITIALIZE_PARAMS));
    init_params.version = NV_ENC_INITIALIZE_PARAMS_VER;

    init_params.encodeGUID   = NV_ENC_CODEC_HEVC_GUID;
    init_params.presetGUID   = NV_ENC_PRESET_P4_GUID;
    init_params.tuningInfo = NV_ENC_TUNING_INFO_LOW_LATENCY;
    init_params.encodeWidth  = aligned_width_;
    init_params.encodeHeight = aligned_height_;
    init_params.darWidth     = aligned_width_;
    init_params.darHeight    = aligned_height_;
    init_params.frameRateNum = 72;
    init_params.frameRateDen = 1;
    init_params.enablePTD    = 1;
    init_params.encodeConfig = &encode_config;

    spdlog::get("illixr")->info("Attempting encoder init...");
    spdlog::get("illixr")->info("  Size: {}x{}", aligned_width_, aligned_height_);
    spdlog::get("illixr")->info("  Bitrate: {}", bitrate_);

    auto status = nvenc_.nvEncInitializeEncoder(encoder_, &init_params);
    if (status != NV_ENC_SUCCESS) {
        spdlog::get("illixr")->warn("Init with config failed ({}), trying with preset defaults...", status);

        // Try with NO custom config - just use preset defaults
        init_params.encodeConfig = nullptr;
        status = nvenc_.nvEncInitializeEncoder(encoder_, &init_params);
    }

    if (status != NV_ENC_SUCCESS) {
        throw std::runtime_error("Encoder init failed: " + std::to_string(status));
    }

    spdlog::get("illixr")->info("Encoder initialized successfully!");

    // Get VPS/SPS/PPS
    get_sequence_headers();

    initialized_ = true;
}

void nvenc_encoder::get_sequence_headers() {
    // Get sequence headers (VPS/SPS/PPS)
    NV_ENC_SEQUENCE_PARAM_PAYLOAD seq_params = {};
    memset(&seq_params, 0, sizeof(NV_ENC_SEQUENCE_PARAM_PAYLOAD));
    seq_params.version = NV_ENC_SEQUENCE_PARAM_PAYLOAD_VER;

    // Allocate a reasonable buffer upfront
    std::vector<uint8_t> buffer(1024);
    seq_params.spsppsBuffer = buffer.data();
    seq_params.inBufferSize = buffer.size();

    uint32_t actual_size = 0;
    seq_params.outSPSPPSPayloadSize = &actual_size;

    check_nvenc(nvenc_.nvEncGetSequenceParams(encoder_, &seq_params), "Could not get HEVC headers");
    if (actual_size > 0) {
        vps_sps_pps_.assign(buffer.begin(), buffer.begin() + actual_size);
        spdlog::get("illixr")->info("Got HEVC headers: {} bytes", actual_size);

        // Log NAL types for debugging
        log_hevc_profile();
    }
}

void nvenc_encoder::log_hevc_profile() {
    // Parse HEVC SPS to get profile
    // VPS is NAL type 32, SPS is NAL type 33, PPS is NAL type 34
    for (size_t i = 0; i < vps_sps_pps_.size() - 5; i++) {
        if (vps_sps_pps_[i] == 0 && vps_sps_pps_[i+1] == 0 &&
            vps_sps_pps_[i+2] == 0 && vps_sps_pps_[i+3] == 1) {

            uint8_t nal_type = (vps_sps_pps_[i+4] >> 1) & 0x3F;

            if (nal_type == 33) {  // SPS
                // Profile is in profile_tier_level structure
                // For Main profile: general_profile_idc = 1
                // For Main10: general_profile_idc = 2
                spdlog::get("illixr")->info("Found HEVC SPS at offset {}", i);

                // The profile info is a bit complex to parse in HEVC
                // For now just log raw bytes after NAL header
                if (i + 10 < vps_sps_pps_.size()) {
                    spdlog::get("illixr")->info("SPS bytes: {:02X} {:02X} {:02X} {:02X} {:02X} {:02X}",
                        vps_sps_pps_[i+4], vps_sps_pps_[i+5], vps_sps_pps_[i+6],
                        vps_sps_pps_[i+7], vps_sps_pps_[i+8], vps_sps_pps_[i+9]);
                }
            }
        }
    }
}

void nvenc_encoder::create_buffers() {
    // Create the input buffer
    NV_ENC_CREATE_INPUT_BUFFER create_input_buffer = {};
    memset(&create_input_buffer, 0, sizeof(NV_ENC_CREATE_INPUT_BUFFER));
    create_input_buffer.version   = NV_ENC_CREATE_INPUT_BUFFER_VER;
    create_input_buffer.width     = aligned_width_;
    create_input_buffer.height    = aligned_height_;
    create_input_buffer.bufferFmt = NV_ENC_BUFFER_FORMAT_NV12;

    check_nvenc(nvenc_.nvEncCreateInputBuffer(encoder_, &create_input_buffer), "Create input buffer failed");
    input_buffer_ = create_input_buffer.inputBuffer;

    // Output buffer
    NV_ENC_CREATE_BITSTREAM_BUFFER create_output = {};
    create_output.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;

    check_nvenc(nvenc_.nvEncCreateBitstreamBuffer(encoder_, &create_output), "Create output buffer");
    output_buffer_ = create_output.bitstreamBuffer;

    spdlog::get("illixr")->info("Created input/output buffers");
}

nvenc_encoder::~nvenc_encoder() {
    if (initialized_) {
        if (output_buffer_)
            nvenc_.nvEncDestroyBitstreamBuffer(encoder_, output_buffer_);
        if (input_buffer_)
            nvenc_.nvEncDestroyInputBuffer(encoder_, input_buffer_);
        if (encoder_)
            nvenc_.nvEncDestroyEncoder(encoder_);
    }
    if (cu_context_)
        cuCtxDestroy(cu_context_);
}

void nvenc_encoder::convert_bgr_to_nv12(const cv::Mat& bgr, uint8_t* nv12, uint32_t pitch) const {
    // Handle size mismatch
    cv::Mat input = bgr;
    if (bgr.cols != width_ || bgr.rows != height_) {
        cv::resize(bgr, input, cv::Size(width_, height_));
    }

    // Convert BGR to YUV I420
    cv::Mat yuv;
    cv::cvtColor(bgr, yuv, cv::COLOR_BGR2YUV_I420);
    // Copy Y plane with pitch
    for (int y = 0; y < height_; y++) {
        memcpy(nv12 + y * pitch, yuv.data + y * width_, width_);
    }

    // Pad Y plane if needed
    if (aligned_height_ > height_) {
        for (int y = height_; y < aligned_height_; y++) {
            memset(nv12 + y * pitch, 16, pitch);  // Black in Y
        }
    }

    // Interleave U and V into UV plane
    uint8_t* uv_dst = nv12 + pitch * aligned_height_;
    uint8_t* u_src = yuv.data + width_ * height_;
    uint8_t* v_src = u_src + (width_ * height_ / 4);

    for (int y = 0; y < height_ / 2; y++) {
        for (int x = 0; x < width_ / 2; x++) {
            int src_idx = y * (width_ / 2) + x;
            int dst_idx = y * pitch + x * 2;
            uv_dst[dst_idx] = u_src[src_idx];
            uv_dst[dst_idx + 1] = v_src[src_idx];
        }
        // Pad UV row if needed
        if (aligned_width_ > width_) {
            for (int x = width_; x < aligned_width_; x += 2) {
                int dst_idx = y * pitch + x;
                uv_dst[dst_idx] = 128;      // Neutral U
                uv_dst[dst_idx + 1] = 128;  // Neutral V
            }
        }
    }

    // Pad UV rows if needed
    if (aligned_height_ > height_) {
        for (int y = height_ / 2; y < aligned_height_ / 2; y++) {
            for (int x = 0; x < aligned_width_; x += 2) {
                int dst_idx = y * pitch + x;
                uv_dst[dst_idx] = 128;
                uv_dst[dst_idx + 1] = 128;
            }
        }
    }

}

std::vector<uint8_t> nvenc_encoder::encode(const cv::Mat& frame) {
    if (!initialized_) {
        spdlog::get("illixr")->error("Encoder not initialized");
        throw std::runtime_error("Encoder not initialized");
    }
    if (frame.cols != width_ || frame.rows != height_) {
        spdlog::get("illixr")->error("Frame size mismatch {}x{} should be {} {}", frame.cols, frame.rows,
            width_, height_);
        throw std::runtime_error("Frame size mismatch");
    }
    spdlog::get("illixr")->debug("Compressing");

    // Lock input buffer and copy frame data
    NV_ENC_LOCK_INPUT_BUFFER lock_input_buffer = {};
    memset(&lock_input_buffer, 0, sizeof(NV_ENC_LOCK_INPUT_BUFFER));
    lock_input_buffer.version     = NV_ENC_LOCK_INPUT_BUFFER_VER;
    lock_input_buffer.inputBuffer = input_buffer_;

    check_nvenc(nvenc_.nvEncLockInputBuffer(encoder_, &lock_input_buffer), "Lock input buffer failed");

    // Convert BGR to NV12
    convert_bgr_to_nv12(frame, static_cast<uint8_t*>(lock_input_buffer.bufferDataPtr), lock_input_buffer.pitch);

    check_nvenc(nvenc_.nvEncUnlockInputBuffer(encoder_, input_buffer_), "Unlock input");

    // Encode frame
    NV_ENC_PIC_PARAMS pic_params = {};
    memset(&pic_params, 0, sizeof(NV_ENC_PIC_PARAMS));
    pic_params.version         = NV_ENC_PIC_PARAMS_VER;
    pic_params.inputBuffer     = input_buffer_;
    pic_params.bufferFmt       = NV_ENC_BUFFER_FORMAT_NV12;
    pic_params.inputWidth      = aligned_width_;
    pic_params.inputHeight     = aligned_height_;
    pic_params.outputBitstream = output_buffer_;
    pic_params.pictureStruct   = NV_ENC_PIC_STRUCT_FRAME;
    pic_params.inputTimeStamp  = frame_count_++;

    check_nvenc(nvenc_.nvEncEncodePicture(encoder_, &pic_params), "Encode picture failed");

    // Lock output bitstream and copy encoded data
    NV_ENC_LOCK_BITSTREAM lock_bit_stream = {};
    memset(&lock_bit_stream, 0, sizeof(NV_ENC_LOCK_BITSTREAM));
    lock_bit_stream.version         = NV_ENC_LOCK_BITSTREAM_VER;
    lock_bit_stream.outputBitstream = output_buffer_;

    check_nvenc(nvenc_.nvEncLockBitstream(encoder_, &lock_bit_stream), "Lock bitstream failed");

    // Copy encoded data
    std::vector<uint8_t> encoded_data;
    const auto           data = static_cast<uint8_t*>(lock_bit_stream.bitstreamBufferPtr);
    const size_t size = lock_bit_stream.bitstreamSizeInBytes;

    // Prepend VPS/SPS/PPS to IDR frames
    if (lock_bit_stream.pictureType == NV_ENC_PIC_TYPE_IDR && !vps_sps_pps_.empty()) {
        encoded_data.reserve(vps_sps_pps_.size() + size);
        encoded_data.insert(encoded_data.end(), vps_sps_pps_.begin(), vps_sps_pps_.end());
    }

    encoded_data.insert(encoded_data.end(), data, data + size);

    check_nvenc(nvenc_.nvEncUnlockBitstream(encoder_, output_buffer_), "Unlock bitstream");

    return encoded_data;
}

std::future<std::vector<uint8_t>> nvenc_encoder::encode_async(const cv::Mat& frame) {
    return std::async(std::launch::async, [this, frame]() {
        return encode(frame);
    });
}
