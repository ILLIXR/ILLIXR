#include "nvenc_encoder.hpp"

#include <memory>
#include <spdlog/spdlog.h>
#include <stdexcept>

using namespace ILLIXR;

const uint8_t nvenc_encoder::NAL_START_CODE[4] = {0x00, 0x00, 0x00, 0x01};

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

#include <opencv2/opencv.hpp>
#include <cuda_runtime.h>
#include <nvEncodeAPI.h>
#include <vector>
#include <memory>
#include <future>
#include <stdexcept>

// Load NVENC API functions
typedef NVENCSTATUS (NVENCAPI* PNVENCODEAPICREATEINSTANCE)(NV_ENCODE_API_FUNCTION_LIST*);

// Helper function to identify preset GUIDs
static const char* getPresetName(const GUID& guid) {
    // Compare GUIDs
    auto guidEquals = [](const GUID& a, const GUID& b) {
        return memcmp(&a, &b, sizeof(GUID)) == 0;
    };

    if (guidEquals(guid, NV_ENC_CODEC_PROFILE_AUTOSELECT_GUID)) return "NV_ENC_CODEC_PROFILE_AUTOSELECT_GUID";
    if (guidEquals(guid, NV_ENC_H264_PROFILE_BASELINE_GUID)) return "NV_ENC_H264_PROFILE_BASELINE_GUID";
    if (guidEquals(guid, NV_ENC_H264_PROFILE_MAIN_GUID)) return "NV_ENC_H264_PROFILE_MAIN_GUID";
    if (guidEquals(guid, NV_ENC_H264_PROFILE_HIGH_GUID)) return "NV_ENC_H264_PROFILE_HIGH_GUID";
    if (guidEquals(guid, NV_ENC_H264_PROFILE_HIGH_444_GUID)) return "NV_ENC_H264_PROFILE_HIGH_444_GUID";
    if (guidEquals(guid, NV_ENC_H264_PROFILE_STEREO_GUID)) return "NV_ENC_H264_PROFILE_STEREO_GUID";
    if (guidEquals(guid, NV_ENC_H264_PROFILE_PROGRESSIVE_HIGH_GUID)) return "NV_ENC_H264_PROFILE_PROGRESSIVE_HIGH_GUID";
    if (guidEquals(guid, NV_ENC_H264_PROFILE_CONSTRAINED_HIGH_GUID)) return "NV_ENC_H264_PROFILE_CONSTRAINED_HIGH_GUID";
    if (guidEquals(guid, NV_ENC_HEVC_PROFILE_MAIN_GUID)) return "NV_ENC_HEVC_PROFILE_MAIN_GUID";
    if (guidEquals(guid, NV_ENC_HEVC_PROFILE_MAIN10_GUID)) return "NV_ENC_HEVC_PROFILE_MAIN10_GUID";
    if (guidEquals(guid, NV_ENC_HEVC_PROFILE_FREXT_GUID)) return "NV_ENC_HEVC_PROFILE_FREXT_GUID";
    if (guidEquals(guid, NV_ENC_AV1_PROFILE_MAIN_GUID)) return "NV_ENC_AV1_PROFILE_MAIN_GUID";

    if (guidEquals(guid, NV_ENC_CODEC_H264_GUID)) return "H264";
    if (guidEquals(guid, NV_ENC_CODEC_HEVC_GUID)) return "HVEC";
    if (guidEquals(guid, NV_ENC_CODEC_AV1_GUID)) return "AV1";
    // Modern presets (SDK 10.0+)
    if (guidEquals(guid, NV_ENC_PRESET_P1_GUID)) return "P1 (Highest Quality)";
    if (guidEquals(guid, NV_ENC_PRESET_P2_GUID)) return "P2 (High Quality)";
    if (guidEquals(guid, NV_ENC_PRESET_P3_GUID)) return "P3 (Quality)";
    if (guidEquals(guid, NV_ENC_PRESET_P4_GUID)) return "P4 (Balanced)";
    if (guidEquals(guid, NV_ENC_PRESET_P5_GUID)) return "P5 (Fast)";
    if (guidEquals(guid, NV_ENC_PRESET_P6_GUID)) return "P6 (Faster)";
    if (guidEquals(guid, NV_ENC_PRESET_P7_GUID)) return "P7 (Fastest)";

    // Legacy presets
    //if (guidEquals(guid, NV_ENC_PRESET_DEFAULT_GUID)) return "Default";
    //if (guidEquals(guid, NV_ENC_PRESET_HP_GUID)) return "High Performance (Legacy)";
    //if (guidEquals(guid, NV_ENC_PRESET_HQ_GUID)) return "High Quality (Legacy)";
    //if (guidEquals(guid, NV_ENC_PRESET_BD_GUID)) return "Bluray Disk (Legacy)";
    //if (guidEquals(guid, NV_ENC_PRESET_LOW_LATENCY_DEFAULT_GUID)) return "Low Latency Default (Legacy)";
    //if (guidEquals(guid, NV_ENC_PRESET_LOW_LATENCY_HQ_GUID)) return "Low Latency HQ (Legacy)";
    //if (guidEquals(guid, NV_ENC_PRESET_LOW_LATENCY_HP_GUID)) return "Low Latency HP (Legacy)";
    //if (guidEquals(guid, NV_ENC_PRESET_LOSSLESS_DEFAULT_GUID)) return "Lossless Default (Legacy)";
    //if (guidEquals(guid, NV_ENC_PRESET_LOSSLESS_HP_GUID)) return "Lossless HP (Legacy)";

    return "Unknown Preset";
}

// Helper function to find NAL units
std::vector<uint8_t> nvenc_encoder::extract_sps_pps(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> headers;

    for (size_t i = 0; i < data.size() - 4; i++) {
        // Look for start code (0x00 0x00 0x00 0x01 or 0x00 0x00 0x01)
        bool found_start = false;
        size_t nal_start = 0;

        if (i < data.size() - 3 &&
            data[i] == 0 && data[i+1] == 0 && data[i+2] == 0 && data[i+3] == 1) {
            found_start = true;
            nal_start = i + 4;
            } else if (i < data.size() - 2 &&
                       data[i] == 0 && data[i+1] == 0 && data[i+2] == 1) {
                found_start = true;
                nal_start = i + 3;
                       }

        if (found_start && nal_start < data.size()) {
            uint8_t nal_header = data[nal_start];
            uint8_t nal_type = nal_header & 0x1F;

            spdlog::get("illixr")->debug("Found NAL at {}, header: 0x{:02x}, type: {}",
                                         i, nal_header, nal_type);

            // NAL type 7 = SPS, 8 = PPS, 5 = IDR slice
            if (nal_type == 7 || nal_type == 8) {
                spdlog::get("illixr")->info("Found {} at position {}",
                                           nal_type == 7 ? "SPS" : "PPS", i);

                // Find next start code or end of data
                size_t end = nal_start;
                while (end < data.size() - 3) {
                    if ((data[end] == 0 && data[end+1] == 0 && data[end+2] == 0 && data[end+3] == 1) ||
                        (data[end] == 0 && data[end+1] == 0 && data[end+2] == 1)) {
                        break;
                        }
                    end++;
                }
                if (end >= data.size() - 3) end = data.size();

                // Copy this NAL unit (including start code)
                headers.insert(headers.end(), data.begin() + i, data.begin() + end);

                // Skip past this NAL unit
                i = end - 1;
            }
        }
    }

    return headers;
}
[[maybe_unused]] nvenc_encoder::nvenc_encoder(int w, int h)
    : width_(w)
    , height_(h) {
    // Initialize CUDA
    check_cuda(cuInit(0), "CUDA init failed");

    CUdevice cu_device;
    check_cuda(cuDeviceGet(&cu_device, 0), "Get CUDA device failed");
    char szDeviceName[80];
    check_cuda(cuDeviceGetName(szDeviceName, sizeof(szDeviceName), cu_device), "");
    spdlog::get("illixr")->debug("GPU in use: {}", szDeviceName);
    check_cuda(cuCtxCreate(&cu_context_, 0, cu_device), "Create CUDA context failed");

    // Load NVENC library
    uint32_t version = 0;
    uint32_t currentVersion = (NVENCAPI_MAJOR_VERSION << 4) | NVENCAPI_MINOR_VERSION;
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

    auto createInstance = (PNVENCODEAPICREATEINSTANCE)
#ifdef _WIN32
        GetProcAddress(nvenc_lib, "NvEncodeAPICreateInstance");
#else
        dlsym(nvenc_lib, "NvEncodeAPICreateInstance");
#endif

    if (!createInstance) {
        throw std::runtime_error("Failed to get NvEncodeAPICreateInstance");
    }
    memset(&nvenc_, 0, sizeof(NV_ENCODE_API_FUNCTION_LIST));
    nvenc_.version = NV_ENCODE_API_FUNCTION_LIST_VER;
    check_nvenc(createInstance(&nvenc_), "Create NVENC instance failed");

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

    // Initialize encoder
    NV_ENC_INITIALIZE_PARAMS init_params = {};
    memset(&init_params, 0, sizeof(NV_ENC_INITIALIZE_PARAMS));
    init_params.version = NV_ENC_INITIALIZE_PARAMS_VER;

    init_params.encodeGUID   = NV_ENC_CODEC_H264_GUID;
    init_params.presetGUID   = NV_ENC_PRESET_P4_GUID;
    init_params.encodeWidth  = width_;
    init_params.encodeHeight = height_;
    init_params.darWidth     = width_;
    init_params.darHeight    = height_;
    init_params.frameRateNum = 30;
    init_params.frameRateDen = 1;
    init_params.enablePTD    = 0;
    init_params.tuningInfo   = NV_ENC_TUNING_INFO_LOW_LATENCY;  // Add tuning info
    init_params.encodeConfig = nullptr;  // Use preset defaults


    // Get preset config as base
    NV_ENC_PRESET_CONFIG preset_config = {};
    memset(&preset_config, 0, sizeof(NV_ENC_PRESET_CONFIG));
    preset_config.version = NV_ENC_PRESET_CONFIG_VER;
    preset_config.presetCfg.version = NV_ENC_CONFIG_VER;

    NVENCSTATUS preset_status = nvenc_.nvEncGetEncodePresetConfigEx(
        encoder_,
        NV_ENC_CODEC_H264_GUID,
        NV_ENC_PRESET_P4_GUID,
        NV_ENC_TUNING_INFO_LOW_LATENCY,  // Required for Ex version
        &preset_config
    );
    if (preset_status != NV_ENC_SUCCESS) {
        spdlog::get("illixr")->error("Failed to get preset: {}", preset_status);
        throw std::runtime_error("Preset config failed");
    }

    // Copy preset as base
    NV_ENC_CONFIG encode_config = {};
    memcpy(&encode_config, &preset_config.presetCfg, sizeof(NV_ENC_CONFIG));

    // FORCE Main profile
    encode_config.profileGUID = NV_ENC_H264_PROFILE_MAIN_GUID;

    // CRITICAL: Force 8-bit depth (prevents High 10)
    encode_config.encodeCodecConfig.h264Config.inputBitDepth = NV_ENC_BIT_DEPTH_8;   // 0 = 8-bit
    encode_config.encodeCodecConfig.h264Config.outputBitDepth = NV_ENC_BIT_DEPTH_8;  // 0 = 8-bit

    // Force Main profile compatible settings
    encode_config.encodeCodecConfig.h264Config.chromaFormatIDC = 1;  // 4:2:0
    encode_config.encodeCodecConfig.h264Config.entropyCodingMode = NV_ENC_H264_ENTROPY_CODING_MODE_CAVLC;
    encode_config.encodeCodecConfig.h264Config.maxNumRefFrames = 1;
    encode_config.encodeCodecConfig.h264Config.level = NV_ENC_LEVEL_H264_41;  // Lower level

    // Disable High/High10 features
    encode_config.encodeCodecConfig.h264Config.bdirectMode = NV_ENC_H264_BDIRECT_MODE_DISABLE;
    encode_config.encodeCodecConfig.h264Config.adaptiveTransformMode = NV_ENC_H264_ADAPTIVE_TRANSFORM_DISABLE;
    encode_config.encodeCodecConfig.h264Config.fmoMode = NV_ENC_H264_FMO_DISABLE;

    // Rate control
    encode_config.rcParams.rateControlMode = NV_ENC_PARAMS_RC_CBR;
    encode_config.rcParams.averageBitRate = 5000000;
    encode_config.rcParams.maxBitRate = 5000000;
    encode_config.rcParams.vbvBufferSize = 5000000;

    encode_config.encodeCodecConfig.h264Config.repeatSPSPPS = 1;

    init_params.encodeConfig = &encode_config;

    check_nvenc(nvenc_.nvEncInitializeEncoder(encoder_, &init_params), "Initialize encoder failed");

    // Get sequence headers (SPS/PPS)
    NV_ENC_SEQUENCE_PARAM_PAYLOAD seq_params = {};
    memset(&seq_params, 0, sizeof(NV_ENC_SEQUENCE_PARAM_PAYLOAD));
    seq_params.version = NV_ENC_SEQUENCE_PARAM_PAYLOAD_VER;

    // Allocate a reasonable buffer upfront
    std::vector<uint8_t> temp_buffer(1024);  // SPS/PPS are usually < 100 bytes
    seq_params.spsppsBuffer = temp_buffer.data();
    seq_params.inBufferSize = temp_buffer.size();

    uint32_t actual_size = 0;
    seq_params.outSPSPPSPayloadSize = &actual_size;

    NVENCSTATUS status;
    if ((status = nvenc_.nvEncGetSequenceParams(encoder_, &seq_params)) == NV_ENC_SUCCESS) {
        sps_pps_data_.assign(temp_buffer.begin(), temp_buffer.begin() + actual_size);
        has_sps_pps_ = true;

        if (actual_size > 4) {
            uint8_t profile_idc = temp_buffer[4];
            const char* profile_name = "Unknown";
            if (profile_idc == 66) profile_name = "Baseline";
            else if (profile_idc == 77) profile_name = "Main";
            else if (profile_idc == 100) profile_name = "High";
            else if (profile_idc == 83) profile_name = "Scalable High (bad)";
            else if (profile_idc == 86) profile_name = "Scalable High (bad)";
            else if (profile_idc == 103) profile_name = "High 10 Intra (bad)";
            else if (profile_idc == 110) profile_name = "High 10 (bad)";

            spdlog::get("illixr")->info("ACTUAL encoder profile: {} (IDC: {})", profile_name, profile_idc);

            if (profile_idc != 77 && profile_idc != 66) {
                spdlog::get("illixr")->error("ERROR: Encoder is NOT using Main/Baseline Profile!");
                spdlog::get("illixr")->error("This WILL NOT work on Quest 3!");
                spdlog::get("illixr")->error("Profile IDC should be 77 (Main) or 66 (Baseline), got {}", profile_idc);
            }
        }

        // Debug: print SPS/PPS hex
        spdlog::get("illixr")->info("Got SPS/PPS: {} bytes", actual_size);
        std::string hex_dump;
        for (size_t i = 0; i < std::min<size_t>(32, actual_size); i++) {
            char buf[8];
            snprintf(buf, sizeof(buf), "%02x ", sps_pps_data_[i]);
            hex_dump += buf;
        }
        spdlog::get("illixr")->info("SPS/PPS hex: {}", hex_dump);
    } else {
        spdlog::get("illixr")->error("Failed to get sequence params: {}", status);
    }


    // Create the input buffer
    NV_ENC_CREATE_INPUT_BUFFER create_input_buffer = {};
    memset(&create_input_buffer, 0, sizeof(NV_ENC_CREATE_INPUT_BUFFER));
    create_input_buffer.version   = NV_ENC_CREATE_INPUT_BUFFER_VER;
    create_input_buffer.width     = width_;
    create_input_buffer.height    = height_;
    create_input_buffer.bufferFmt = NV_ENC_BUFFER_FORMAT_NV12;

    check_nvenc(nvenc_.nvEncCreateInputBuffer(encoder_, &create_input_buffer), "Create input buffer failed");
    input_buffer_ = create_input_buffer.inputBuffer;

    // Create the output bitstream
    NV_ENC_CREATE_BITSTREAM_BUFFER create_bitstream_buffer;
    create_bitstream_buffer.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;
    check_nvenc(nvenc_.nvEncCreateBitstreamBuffer(encoder_, &create_bitstream_buffer), "Create bitstream buffer failed");
    output_bit_stream_ = create_bitstream_buffer.bitstreamBuffer;

    initialized_ = true;
}

nvenc_encoder::~nvenc_encoder() {
    if (initialized_) {
        if (output_bit_stream_)
            nvenc_.nvEncDestroyBitstreamBuffer(encoder_, output_bit_stream_);
        if (input_buffer_)
            nvenc_.nvEncDestroyInputBuffer(encoder_, input_buffer_);
        if (encoder_)
            nvenc_.nvEncDestroyEncoder(encoder_);
        if (cu_context_)
            cuCtxDestroy(cu_context_);
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

    // Convert BGR to NV12 (YUV 4:2:0)
    //cv::Mat nv12(height_ * 3 / 2, width_, CV_8UC1);
    //cv::cvtColor(frame, nv12, cv::COLOR_BGR2YUV_NV12);
    //memcpy(lock_input_buffer.bufferDataPtr, nv12.data, width_ * height_ * 3 / 2);

    // Convert BGR to NV12 (YUV 4:2:0)
    cv::Mat yuv;
    cv::cvtColor(frame, yuv, cv::COLOR_BGR2YUV_I420);

    // NV12 format: Y plane followed by interleaved UV plane
    // I420 is planar (Y, U, V separate), need to convert to NV12 (Y, UV interleaved)
    uint8_t* dst = static_cast<uint8_t*>(lock_input_buffer.bufferDataPtr);

    // Copy Y plane
    memcpy(dst, yuv.data, width_ * height_);

    // Interleave U and V planes into UV plane for NV12
    uint8_t* uv_dst = dst + width_ * height_;
    uint8_t* u_src = yuv.data + width_ * height_;
    uint8_t* v_src = yuv.data + width_ * height_ + (width_ * height_ / 4);

    for (int i = 0; i < (width_ * height_ / 4); i++) {
        uv_dst[i * 2]     = u_src[i];
        uv_dst[i * 2 + 1] = v_src[i];
    }
    check_nvenc(nvenc_.nvEncUnlockInputBuffer(encoder_, input_buffer_), "Unlock input buffer failed");

    // Encode frame
    NV_ENC_PIC_PARAMS pic_params = {};
    memset(&pic_params, 0, sizeof(NV_ENC_PIC_PARAMS));
    pic_params.version         = NV_ENC_PIC_PARAMS_VER;
    pic_params.inputBuffer     = input_buffer_;
    pic_params.bufferFmt       = NV_ENC_BUFFER_FORMAT_NV12;
    pic_params.inputWidth      = width_;
    pic_params.inputHeight     = height_;
    pic_params.outputBitstream = output_bit_stream_;
    pic_params.pictureStruct   = NV_ENC_PIC_STRUCT_FRAME;

    check_nvenc(nvenc_.nvEncEncodePicture(encoder_, &pic_params), "Encode picture failed");

    // Lock output bitstream and copy encoded data
    NV_ENC_LOCK_BITSTREAM lock_bit_stream = {};
    memset(&lock_bit_stream, 0, sizeof(NV_ENC_LOCK_BITSTREAM));
    lock_bit_stream.version         = NV_ENC_LOCK_BITSTREAM_VER;
    lock_bit_stream.outputBitstream = output_bit_stream_;

    check_nvenc(nvenc_.nvEncLockBitstream(encoder_, &lock_bit_stream), "Lock bitstream failed");

    // Get frame data
    uint8_t* frame_data = static_cast<uint8_t*>(lock_bit_stream.bitstreamBufferPtr);
    size_t frame_size = lock_bit_stream.bitstreamSizeInBytes;

    std::vector<uint8_t> encoded_data;

    // Prepend SPS/PPS to every IDR frame
    bool is_idr = (lock_bit_stream.pictureType == NV_ENC_PIC_TYPE_IDR);

    if (is_idr && has_sps_pps_) {
        encoded_data.reserve(sps_pps_data_.size() + frame_size);
        encoded_data.insert(encoded_data.end(), sps_pps_data_.begin(), sps_pps_data_.end());
        spdlog::get("illixr")->debug("Prepended SPS/PPS to IDR frame");
    }

    // Append frame data
    encoded_data.insert(encoded_data.end(), frame_data, frame_data + frame_size);

    check_nvenc(nvenc_.nvEncUnlockBitstream(encoder_, output_bit_stream_), "Unlock bitstream failed");
    spdlog::get("illixr")->debug("Compressed frame {}x{}", width_, height_);
    return encoded_data;
}

std::future<std::vector<uint8_t>> nvenc_encoder::encode_async(const cv::Mat& frame) {
    return std::async(std::launch::async, [this, frame]() {
        return encode(frame);
    });
}
