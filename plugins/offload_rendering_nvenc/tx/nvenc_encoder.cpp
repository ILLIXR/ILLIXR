#include "nvenc_encoder.hpp"

#include <memory>
#include <spdlog/spdlog.h>
#include <stdexcept>

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

    // Modern presets (SDK 10.0+)
    if (guidEquals(guid, NV_ENC_PRESET_P1_GUID)) return "P1 (Highest Quality)";
    if (guidEquals(guid, NV_ENC_PRESET_P2_GUID)) return "P2 (High Quality)";
    if (guidEquals(guid, NV_ENC_PRESET_P3_GUID)) return "P3 (Quality)";
    if (guidEquals(guid, NV_ENC_PRESET_P4_GUID)) return "P4 (Balanced)";
    if (guidEquals(guid, NV_ENC_PRESET_P5_GUID)) return "P5 (Fast)";
    if (guidEquals(guid, NV_ENC_PRESET_P6_GUID)) return "P6 (Faster)";
    if (guidEquals(guid, NV_ENC_PRESET_P7_GUID)) return "P7 (Fastest)";

    // Legacy presets
    if (guidEquals(guid, NV_ENC_PRESET_DEFAULT_GUID)) return "Default";
    if (guidEquals(guid, NV_ENC_PRESET_HP_GUID)) return "High Performance (Legacy)";
    if (guidEquals(guid, NV_ENC_PRESET_HQ_GUID)) return "High Quality (Legacy)";
    if (guidEquals(guid, NV_ENC_PRESET_BD_GUID)) return "Bluray Disk (Legacy)";
    if (guidEquals(guid, NV_ENC_PRESET_LOW_LATENCY_DEFAULT_GUID)) return "Low Latency Default (Legacy)";
    if (guidEquals(guid, NV_ENC_PRESET_LOW_LATENCY_HQ_GUID)) return "Low Latency HQ (Legacy)";
    if (guidEquals(guid, NV_ENC_PRESET_LOW_LATENCY_HP_GUID)) return "Low Latency HP (Legacy)";
    if (guidEquals(guid, NV_ENC_PRESET_LOSSLESS_DEFAULT_GUID)) return "Lossless Default (Legacy)";
    if (guidEquals(guid, NV_ENC_PRESET_LOSSLESS_HP_GUID)) return "Lossless HP (Legacy)";

    return "Unknown Preset";
}
[[maybe_unused]] nvenc_encoder::nvenc_encoder(int w, int h)
    : width_(w)
    , height_(h) {
    // Initialize CUDA
    check_cuda(cuInit(0), "CUDA init failed");

    CUdevice cu_device;
    check_cuda(cuDeviceGet(&cu_device, 0), "Get CUDA device failed");
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

    check_nvenc(createInstance(&nvenc_), "Create NVENC instance failed");

    // Open encode session
    NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS session_params;
    session_params.version    = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
    session_params.device     = cu_context_;
    session_params.deviceType = NV_ENC_DEVICE_TYPE_CUDA;
    session_params.apiVersion = NVENCAPI_VERSION;

    check_nvenc(nvenc_.nvEncOpenEncodeSessionEx(&session_params, &encoder_), "Open encode session failed");

    // Initialize encoder
    NV_ENC_INITIALIZE_PARAMS init_params;
    NV_ENC_CONFIG            encode_config;
    encode_config.version = NV_ENC_CONFIG_VER;
    GUID presetGUIDs[32];
    uint32_t count_p = 0;
    check_nvenc(nvenc_.nvEncGetEncodePresetGUIDs(encoder_, NV_ENC_CODEC_H264_GUID, presetGUIDs, 32, &count_p), "Preset failed");

    printf("Available presets: %d\n", count_p);
    for (uint32_t i = 0; i < count_p; i++) {
        const char* presetName = getPresetName(presetGUIDs[i]);
        printf("  Preset %d: %s\n", i, presetName);
    }
    init_params.version = NV_ENC_INITIALIZE_PARAMS_VER;
    // init_params.encodeConfig = &encode_config;
    init_params.encodeGUID   = NV_ENC_CODEC_H264_GUID;
    init_params.presetGUID   = NV_ENC_PRESET_DEFAULT_GUID;
    init_params.encodeWidth  = width_;
    init_params.encodeHeight = height_;
    init_params.darWidth     = width_;
    init_params.darHeight    = height_;
    init_params.frameRateNum = 30;
    init_params.frameRateDen = 1;
    init_params.enablePTD    = 1;

    NV_ENC_PRESET_CONFIG preset_config;
    preset_config.version           = NV_ENC_PRESET_CONFIG_VER;
    preset_config.presetCfg.version = NV_ENC_CONFIG_VER;

    NVENCSTATUS presetStatus = nvenc_.nvEncGetEncodePresetConfigEx(encoder_, NV_ENC_CODEC_H264_GUID, NV_ENC_PRESET_DEFAULT_GUID,
                                                                   NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY, &preset_config);
    if (presetStatus != NV_ENC_SUCCESS) {
        // Fallback for older SDK versions
        preset_config.version           = NV_ENC_PRESET_CONFIG_VER;
        preset_config.presetCfg.version = NV_ENC_CONFIG_VER;
        NVENCSTATUS old_preset_status =
            nvenc_.nvEncGetEncodePresetConfig(encoder_, NV_ENC_CODEC_H264_GUID, NV_ENC_PRESET_P4_GUID, &preset_config);
        if (old_preset_status != NV_ENC_SUCCESS) {
            // Manual configuration as a last resort
            encode_config.version                                = NV_ENC_CONFIG_VER;
            encode_config.gopLength                              = NVENC_INFINITE_GOPLENGTH;
            encode_config.frameIntervalP                         = 1;
            encode_config.profileGUID                            = NV_ENC_H264_PROFILE_MAIN_GUID;
            encode_config.rcParams.rateControlMode               = NV_ENC_PARAMS_RC_CBR;
            encode_config.rcParams.averageBitRate                = 5000000;
            encode_config.rcParams.vbvBufferSize                 = 5000000;
            encode_config.rcParams.maxBitRate                    = 5000000;
            encode_config.encodeCodecConfig.h264Config.idrPeriod = NVENC_INFINITE_GOPLENGTH;
        } else {
            memcpy(&encode_config, &preset_config.presetCfg, sizeof(NV_ENC_CONFIG));
            encode_config.profileGUID              = NV_ENC_H264_PROFILE_MAIN_GUID;
            encode_config.rcParams.rateControlMode = NV_ENC_PARAMS_RC_CBR;
            encode_config.rcParams.averageBitRate  = 5000000;
        }
    } else {
        memcpy(&encode_config, &preset_config.presetCfg, sizeof(NV_ENC_CONFIG));
        encode_config.profileGUID              = NV_ENC_H264_PROFILE_MAIN_GUID;
        encode_config.rcParams.rateControlMode = NV_ENC_PARAMS_RC_CBR;
        encode_config.rcParams.averageBitRate  = 5000000;
    }
    init_params.encodeConfig = &encode_config;
    check_nvenc(nvenc_.nvEncInitializeEncoder(encoder_, &init_params), "Initialize encoder failed");

    // Create the input buffer
    NV_ENC_CREATE_INPUT_BUFFER create_input_buffer;
    create_input_buffer.version   = NV_ENC_CREATE_INPUT_BUFFER_VER;
    create_input_buffer.width     = width_;
    create_input_buffer.height    = height_;
    create_input_buffer.bufferFmt = NV_ENC_BUFFER_FORMAT_ARGB;

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
    if (!initialized_ || frame.cols != width_ || frame.rows != height_) {
        throw std::runtime_error("Encoder not initialized_ or frame size mismatch");
    }

    // Lock input buffer and copy frame data
    NV_ENC_LOCK_INPUT_BUFFER lock_input_buffer;
    lock_input_buffer.version     = NV_ENC_LOCK_INPUT_BUFFER_VER;
    lock_input_buffer.inputBuffer = input_buffer_;

    check_nvenc(nvenc_.nvEncLockInputBuffer(encoder_, &lock_input_buffer), "Lock input buffer failed");

    cv::Mat bgra;
    cv::cvtColor(frame, bgra, cv::COLOR_BGR2BGRA);
    memcpy(lock_input_buffer.bufferDataPtr, bgra.data, width_ * height_ * 4);

    check_nvenc(nvenc_.nvEncUnlockInputBuffer(encoder_, input_buffer_), "Unlock input buffer failed");

    // Encode frame
    NV_ENC_PIC_PARAMS pic_params;
    pic_params.version         = NV_ENC_PIC_PARAMS_VER;
    pic_params.inputBuffer     = input_buffer_;
    pic_params.bufferFmt       = NV_ENC_BUFFER_FORMAT_ARGB;
    pic_params.inputWidth      = width_;
    pic_params.inputHeight     = height_;
    pic_params.outputBitstream = output_bit_stream_;
    pic_params.pictureStruct   = NV_ENC_PIC_STRUCT_FRAME;

    check_nvenc(nvenc_.nvEncEncodePicture(encoder_, &pic_params), "Encode picture failed");

    // Lock output bitstream and copy encoded data
    NV_ENC_LOCK_BITSTREAM lock_bit_stream;
    lock_bit_stream.version         = NV_ENC_LOCK_BITSTREAM_VER;
    lock_bit_stream.outputBitstream = output_bit_stream_;

    check_nvenc(nvenc_.nvEncLockBitstream(encoder_, &lock_bit_stream), "Lock bitstream failed");

    std::vector<uint8_t> encoded_data(static_cast<uint8_t*>(lock_bit_stream.bitstreamBufferPtr),
                                      static_cast<uint8_t*>(lock_bit_stream.bitstreamBufferPtr) +
                                          lock_bit_stream.bitstreamSizeInBytes);

    check_nvenc(nvenc_.nvEncUnlockBitstream(encoder_, output_bit_stream_), "Unlock bitstream failed");

    return encoded_data;
}

std::future<std::vector<uint8_t>> nvenc_encoder::encode_async(const cv::Mat& frame) {
    return std::async(std::launch::async, [this, frame]() {
        return encode(frame);
    });
}
