#include "nvenc_encoder.hpp"

#include <algorithm>
#include <cstring>
#include <spdlog/fmt/fmt.h> // If using formatting
#include <spdlog/spdlog.h>
#include <stdexcept>

using namespace ILLIXR;

[[maybe_unused]] nvenc_encoder::nvenc_encoder(uint32_t width, uint32_t height, int64_t bitrate, int framerate,
                                              encoder_mode mode, encoder_codec codec)
    : width_(width)
    , height_(height)
    , bitrate_(bitrate)
    , framerate_(framerate)
    , mode_(mode)
    , codec_(codec) {
    // Validate input dimensions
    if (width == 0 || height == 0) {
        spdlog::get("illixr")->error("nvenc_encoder: Invalid input dimensions: {}x{}", width, height);
        throw std::invalid_argument("Encoder width and height must be non-zero");
    }

    // Align dimensions to 32 for HEVC/AV1
    aligned_width_  = (width + 31) & ~31;
    aligned_height_ = (height + 31) & ~31;

    spdlog::get("illixr")->info("nvenc_encoder: Created with input {}x{}, aligned {}x{}", width_, height_, aligned_width_,
                                aligned_height_);
}

nvenc_encoder::~nvenc_encoder() {
    // Release imported images
    for (auto& img : imported_images_) {
        release_imported_image(img);
    }
    imported_images_.clear();
    if (initialized_.load() && encoder_) {
        if (registered_nv12_) {
            nvenc_.nvEncUnregisterResource(encoder_, registered_nv12_);
            registered_nv12_ = nullptr;
        }
        if (output_buffer_) {
            nvenc_.nvEncDestroyBitstreamBuffer(encoder_, output_buffer_);
            output_buffer_ = nullptr;
        }
        nvenc_.nvEncDestroyEncoder(encoder_);
        encoder_ = nullptr;
    }
    // Unload the NVENC library now that no encoder session is open
#ifdef _WIN32
    if (nvenc_lib_) {
        FreeLibrary(nvenc_lib_);
        nvenc_lib_ = nullptr;
    }
#else
    if (nvenc_lib_) {
        dlclose(nvenc_lib_);
        nvenc_lib_ = nullptr;
    }
#endif
    // Free CUDA buffers
    if (cuda_nv12_buffer_) {
        cuMemFree(cuda_nv12_buffer_);
        cuda_nv12_buffer_ = 0;
    }
#ifdef COMBINED_ENCODING
    if (cuda_left_rgba_buffer_) {
        cuMemFree(cuda_left_rgba_buffer_);
        cuda_left_rgba_buffer_ = 0;
    }
    if (cuda_right_rgba_buffer_) {
        cuMemFree(cuda_right_rgba_buffer_);
        cuda_right_rgba_buffer_ = 0;
    }
#endif

    if (cu_stream_) {
        cudaStreamDestroy(cu_stream_);
        cu_stream_ = nullptr;
    }

    if (cu_context_) {
        cuCtxDestroy(cu_context_);
        cu_context_ = nullptr;
    }
}

// ============================================================================
// Initialization
// ============================================================================

bool nvenc_encoder::initialize(const vulkan_context& vk_ctx) {
    if (initialized_.load()) {
        spdlog::get("illixr")->warn("nvenc_encoder: Already initialized");
        return true;
    }

    vk_ctx_ = vk_ctx;

    try {
        init_cuda();
        init_nvenc();
        query_capabilities();
        init_encoder();
        create_buffers();
#ifdef DUMP_FRAMES
        init_frame_saver();
#endif

        initialized_ = true;
        spdlog::get("illixr")->info("nvenc_encoder: Initialized successfully with GPU color conversion");
        return true;

    } catch (const std::exception& e) {
        spdlog::get("illixr")->error("nvenc_encoder: Initialization failed: {}", e.what());
        return false;
    }
}

void nvenc_encoder::init_cuda() {
    // Initialize CUDA
    check_cuda(cuInit(0), "CUDA init failed");

    // Get CUDA device count
    int device_count = 0;
    check_cuda(cuDeviceGetCount(&device_count), "Get device count failed");

    if (device_count == 0) {
        throw std::runtime_error("No CUDA devices found");
    }

    // TODO: Match CUDA device to Vulkan device by UUID
    // For now, use device 0
    check_cuda(cuDeviceGet(&cu_device_, 0), "Get CUDA device failed");

    char device_name[256];
    check_cuda(cuDeviceGetName(device_name, sizeof(device_name), cu_device_), "Get device name failed");
    spdlog::get("illixr")->info("nvenc_encoder: CUDA device: {}", device_name);

    // Create CUDA context
    // CUDA 13 changed cuCtxCreate to a 4-argument form that takes a
    // CUctxCreateParams* as its second argument.  On CUDA 12 and older the
    // classic 3-argument form (flags, device) is used.
#if CUDA_VERSION >= 13000
    CUctxCreateParams ctx_params = {};
    check_cuda(cuCtxCreate(&cu_context_, &ctx_params, 0, cu_device_), "Create CUDA context failed");
#else
    check_cuda(cuCtxCreate(&cu_context_, 0, cu_device_), "Create CUDA context failed");
#endif

    // Create a CUDA stream for async operations
    check_cuda_runtime(cudaStreamCreate(&cu_stream_), "Create CUDA stream failed");
}

void nvenc_encoder::init_nvenc() {
    check_cuda(cuCtxSetCurrent(cu_context_), "Set CUDA context failed");

    // Load NVENC library
#ifdef _WIN32
    nvenc_lib_ = LoadLibrary(TEXT("nvEncodeAPI64.dll"));
#else
    nvenc_lib_ = dlopen("libnvidia-encode.so.1", RTLD_LAZY);
#endif
    if (!nvenc_lib_) {
        throw std::runtime_error("Failed to load NVENC library");
    }

    auto create_instance = reinterpret_cast<PNVENCODEAPICREATEINSTANCE>(
#ifdef _WIN32
        GetProcAddress(nvenc_lib_, "NvEncodeAPICreateInstance")
#else
        dlsym(nvenc_lib_, "NvEncodeAPICreateInstance")
#endif
    );

    if (!create_instance) {
        throw std::runtime_error("Failed to get NvEncodeAPICreateInstance");
    }

    using get_max_supported_version_fn = NVENCSTATUS(NVENCAPI*)(uint32_t*);
    auto get_max_supported_version = reinterpret_cast<get_max_supported_version_fn>(
#ifdef _WIN32
        GetProcAddress(nvenc_lib_, "NvEncodeAPIGetMaxSupportedVersion")
#else
        dlsym(nvenc_lib_, "NvEncodeAPIGetMaxSupportedVersion")
#endif
    );
    if (get_max_supported_version != nullptr) {
        uint32_t driver_version = 0;
        check_nvenc(get_max_supported_version(&driver_version), "Query maximum NVENC API version failed");
        const uint32_t compiled_version = (NVENCAPI_MAJOR_VERSION << 4U) | NVENCAPI_MINOR_VERSION;
        const uint32_t selected_version = std::min(driver_version, compiled_version);
        nvenc_api_version_ = (selected_version >> 4U) | ((selected_version & 0xFU) << 24U);
        spdlog::get("illixr")->info("nvenc_encoder: NVENC API header {}.{}, driver {}.{}, selected {}.{}",
                                    NVENCAPI_MAJOR_VERSION, NVENCAPI_MINOR_VERSION, driver_version >> 4U,
                                    driver_version & 0xFU, selected_version >> 4U, selected_version & 0xFU);
    }

    memset(&nvenc_, 0, sizeof(nvenc_));
    nvenc_.version = nvenc_struct_version(NV_ENCODE_API_FUNCTION_LIST_VER);
    check_nvenc(create_instance(&nvenc_), "Create NVENC instance failed");

    if (!nvenc_.nvEncOpenEncodeSessionEx) {
        cuCtxDestroy(cu_context_);
        throw std::runtime_error("nvEncOpenEncodeSessionEx not available");
    }

    // Open encode session with CUDA device
    NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS session_params = {};
    memset(&session_params, 0, sizeof(NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS));
    session_params.version    = nvenc_struct_version(NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER);
    session_params.device     = cu_context_;
    session_params.deviceType = NV_ENC_DEVICE_TYPE_CUDA;
    session_params.apiVersion = nvenc_api_version_;

    check_nvenc(nvenc_.nvEncOpenEncodeSessionEx(&session_params, &encoder_), "Open encode session failed");

    spdlog::get("illixr")->info("nvenc_encoder: NVENC session opened");
}

uint32_t nvenc_encoder::nvenc_struct_version(uint32_t compiled_version) const {
    // Structure versions embed the API major in bits 0..7 and the API minor in
    // bits 24..27. Preserve the structure revision and flag bits while replacing
    // only those API fields with the version selected for the installed driver.
    constexpr uint32_t api_version_mask = 0x0F0000FFU;
    return (compiled_version & ~api_version_mask) | nvenc_api_version_;
}

void nvenc_encoder::query_capabilities() {
    // Query supported codecs
    uint32_t codec_count = 0;
    nvenc_.nvEncGetEncodeGUIDCount(encoder_, &codec_count);

    std::vector<GUID> codecs(codec_count);
    nvenc_.nvEncGetEncodeGUIDs(encoder_, codecs.data(), codec_count, &codec_count);

    bool hevc_supported = false;
    bool av1_supported  = false;
    for (const auto& guid : codecs) {
        if (memcmp(&guid, &NV_ENC_CODEC_HEVC_GUID, sizeof(GUID)) == 0) {
            hevc_supported = true;
        }
#ifdef USE_AV1
        if (memcmp(&guid, &NV_ENC_CODEC_AV1_GUID, sizeof(GUID)) == 0) {
            av1_supported = true;
        }
#endif // USE_AV1
    }

    if (!hevc_supported) {
        throw std::runtime_error("HEVC encoding not supported on this GPU");
    }
    spdlog::get("illixr")->info("nvenc_encoder: HEVC encoding supported");

#ifdef USE_AV1
    if (!av1_supported) {
        throw std::runtime_error("AV1 encoding not supported on this GPU (requires Ada Lovelace / RTX 40-series or later)");
    }
    spdlog::get("illixr")->info("nvenc_encoder: AV1 encoding supported");
#endif // USE_AV1
}

void nvenc_encoder::init_encoder() {
    // Get preset config as base
    NV_ENC_PRESET_CONFIG preset_config = {};
    memset(&preset_config, 0, sizeof(NV_ENC_PRESET_CONFIG));
    preset_config.version           = nvenc_struct_version(NV_ENC_PRESET_CONFIG_VER);
    preset_config.presetCfg.version = nvenc_struct_version(NV_ENC_CONFIG_VER);

#ifdef USE_AV1
    const bool use_av1 = (codec_ == encoder_codec::av1);
#else
    constexpr bool use_av1 = false;
#endif // USE_AV1

    const GUID codec_guid = use_av1 ? NV_ENC_CODEC_AV1_GUID : NV_ENC_CODEC_HEVC_GUID;

    // Preset and tuning selection. The native Boba path uses AV1 P5 with the
    // same LowLatency tuning selected by the active ALVR Quality profile.
    // HEVC keeps P7 low-latency since it is less computationally expensive.
    const GUID preset_guid = use_av1 ? NV_ENC_PRESET_P5_GUID : NV_ENC_PRESET_P7_GUID;
    const auto tuning_info = NV_ENC_TUNING_INFO_LOW_LATENCY;

    check_nvenc(nvenc_.nvEncGetEncodePresetConfigEx(encoder_, codec_guid, preset_guid, tuning_info, &preset_config),
                "Failed to get preset: {}");

    // Copy preset as base
    NV_ENC_CONFIG encode_config = {};
    memcpy(&encode_config, &preset_config.presetCfg, sizeof(NV_ENC_CONFIG));
    encode_config.version = nvenc_struct_version(NV_ENC_CONFIG_VER);

#ifdef USE_AV1
    if (use_av1) {
        // AV1 profile: Main (0) — the only profile supported by NVENC AV1.
        // motion_vector mode uses 10-bit to preserve RGBA16F precision; AV1
        // carries this in the High profile but NVENC exposes it via the
        // inputBitDepth / outputBitDepth fields with the Main profile GUID.
        encode_config.profileGUID = NV_ENC_AV1_PROFILE_MAIN_GUID;

        if (mode_ == encoder_mode::motion_vector) {
            encode_config.encodeCodecConfig.av1Config.inputBitDepth  = NV_ENC_BIT_DEPTH_10;
            encode_config.encodeCodecConfig.av1Config.outputBitDepth = NV_ENC_BIT_DEPTH_10;
        } else {
            encode_config.encodeCodecConfig.av1Config.inputBitDepth  = NV_ENC_BIT_DEPTH_8;
            encode_config.encodeCodecConfig.av1Config.outputBitDepth = NV_ENC_BIT_DEPTH_8;
        }

        encode_config.encodeCodecConfig.av1Config.chromaFormatIDC = 1; // 4:2:0
        encode_config.encodeCodecConfig.av1Config.repeatSeqHdr    = 1; // Include OBU headers with each key frame
        // Boba's RGBA source is converted with a full-range BT.709 matrix. The
        // sequence header must describe that exact conversion or MediaCodec may
        // assume limited range and produce a bright, clipped image.
        encode_config.encodeCodecConfig.av1Config.colorPrimaries = NV_ENC_VUI_COLOR_PRIMARIES_BT709;
        encode_config.encodeCodecConfig.av1Config.transferCharacteristics =
            NV_ENC_VUI_TRANSFER_CHARACTERISTIC_SRGB;
        encode_config.encodeCodecConfig.av1Config.matrixCoefficients = NV_ENC_VUI_MATRIX_COEFFS_BT709;
        encode_config.encodeCodecConfig.av1Config.colorRange         = 1;
        // Single reference frame: sufficient for low-latency VR streaming and
        // reduces per-frame motion search work compared to 2 reference frames.
        encode_config.encodeCodecConfig.av1Config.maxNumRefFramesInDPB = 1;
        // Disable features that might cause issues
        encode_config.encodeCodecConfig.av1Config.enableFilmGrainParams = 0;

        // Rate control - CBR for consistent bitrate
        encode_config.rcParams.rateControlMode = NV_ENC_PARAMS_RC_CBR;
        encode_config.rcParams.averageBitRate  = static_cast<uint32_t>(bitrate_);
        encode_config.rcParams.maxBitRate      = static_cast<uint32_t>(bitrate_);
        encode_config.rcParams.vbvBufferSize   = static_cast<uint32_t>(bitrate_ * 4 / framerate_);
        encode_config.rcParams.vbvInitialDelay = static_cast<uint32_t>(bitrate_ * 4 / framerate_);
        encode_config.rcParams.multiPass       = NV_ENC_TWO_PASS_QUARTER_RESOLUTION;
        encode_config.rcParams.enableAQ        = 1;

        // GOP settings - no B-frames for low latency.
        // 1-second GOP (framerate_ frames) limits the corruption window after
        // any stream discontinuity (e.g. Unity scene transitions) to at most
        // 1 second.  Longer GOPs improve compression but increase recovery time.
        encode_config.gopLength                             = static_cast<uint32_t>(15);
        encode_config.encodeCodecConfig.av1Config.idrPeriod = static_cast<uint32_t>(15);
        encode_config.frameIntervalP                        = 1; // No B-frames for low latency
    } else
#endif // USE_AV1
    {
        // HEVC Main profile (8-bit, 4:2:0) for color and depth.
        // HEVC Main10 profile (10-bit, 4:2:0) for motion vectors — preserves the
        // extra precision that RGBA16F carries and avoids banding in near-zero-velocity
        // regions that would be visible at 8-bit quantisation.
        if (mode_ == encoder_mode::motion_vector) {
            encode_config.profileGUID                                 = NV_ENC_HEVC_PROFILE_MAIN10_GUID;
            encode_config.encodeCodecConfig.hevcConfig.inputBitDepth  = NV_ENC_BIT_DEPTH_10;
            encode_config.encodeCodecConfig.hevcConfig.outputBitDepth = NV_ENC_BIT_DEPTH_10;
        } else {
            encode_config.profileGUID                                 = NV_ENC_HEVC_PROFILE_MAIN_GUID;
            encode_config.encodeCodecConfig.hevcConfig.inputBitDepth  = NV_ENC_BIT_DEPTH_8;
            encode_config.encodeCodecConfig.hevcConfig.outputBitDepth = NV_ENC_BIT_DEPTH_8;
        }
        encode_config.encodeCodecConfig.hevcConfig.chromaFormatIDC = 1; // 4:2:0
        encode_config.encodeCodecConfig.hevcConfig.repeatSPSPPS    = 1; // Include headers with each IDR
        // Limit reference frames to reduce cascade failures
        encode_config.encodeCodecConfig.hevcConfig.maxNumRefFramesInDPB = 2;
        // Disable features that might cause issues
        encode_config.encodeCodecConfig.hevcConfig.enableAlphaLayerEncoding = 0;

        // Rate control - CBR for consistent bitrate
        encode_config.rcParams.rateControlMode = NV_ENC_PARAMS_RC_CBR;
        encode_config.rcParams.averageBitRate  = static_cast<uint32_t>(bitrate_);
        encode_config.rcParams.maxBitRate      = static_cast<uint32_t>(bitrate_);
        encode_config.rcParams.vbvBufferSize   = static_cast<uint32_t>(bitrate_ * 4 / framerate_);
        encode_config.rcParams.vbvInitialDelay = static_cast<uint32_t>(bitrate_ * 4 / framerate_);
        // GOP settings - no B-frames for low latency.
        // 1-second GOP (framerate_ frames) limits the corruption window after
        // any stream discontinuity (e.g. Unity scene transitions) to at most
        // 1 second.  Longer GOPs improve compression but increase recovery time.
        encode_config.gopLength                              = static_cast<uint32_t>(15);
        encode_config.encodeCodecConfig.hevcConfig.idrPeriod = static_cast<uint32_t>(15);
        encode_config.frameIntervalP                         = 1; // No B-frames for low latency
    }

    // Initialize encoder
    NV_ENC_INITIALIZE_PARAMS init_params = {};
    memset(&init_params, 0, sizeof(NV_ENC_INITIALIZE_PARAMS));
    init_params.version = nvenc_struct_version(NV_ENC_INITIALIZE_PARAMS_VER);

    init_params.encodeGUID   = codec_guid;
    init_params.presetGUID   = preset_guid;
    init_params.tuningInfo   = tuning_info;
    init_params.encodeWidth  = aligned_width_;
    init_params.encodeHeight = aligned_height_;
    init_params.darWidth     = aligned_width_;
    init_params.darHeight    = aligned_height_;
    init_params.frameRateNum = static_cast<uint32_t>(framerate_);
    init_params.frameRateDen = 1;
    init_params.enablePTD    = 1;
    init_params.encodeConfig = &encode_config;

    NVENCSTATUS status = nvenc_.nvEncInitializeEncoder(encoder_, &init_params);
    if (status != NV_ENC_SUCCESS) {
        spdlog::get("illixr")->warn("nvenc_encoder: Init with config failed ({}), trying preset defaults",
                                    static_cast<int>(status));
        init_params.encodeConfig = nullptr;
        status                   = nvenc_.nvEncInitializeEncoder(encoder_, &init_params);
    }

    check_nvenc(status, "Encoder initialization failed");

    // Get sequence headers (OBU Sequence Header for AV1, VPS/SPS/PPS for HEVC)
    get_sequence_headers();
    send_startup_idrs(5);
    spdlog::get("illixr")->info("nvenc_encoder: {} encoder session initialized", use_av1 ? "AV1" : "HEVC");
}

void nvenc_encoder::create_buffers() {
    check_cuda(cuCtxSetCurrent(cu_context_), "Set CUDA context failed");

    // Validate dimensions
    if (aligned_width_ == 0 || aligned_height_ == 0) {
        spdlog::get("illixr")->error("nvenc_encoder: Invalid dimensions: aligned_width={}, aligned_height={} (original: {}x{})",
                                     aligned_width_, aligned_height_, width_, height_);
        throw std::runtime_error("Invalid encoder dimensions (width or height is 0)");
    }

    // Allocate the encoder input buffer.
    //
    // NV12  (color / depth):     8-bit per sample, 1 byte/pixel Y + 0.5 byte/pixel UV
    // P010  (motion_vector):    10-bit per sample, 2 bytes/pixel Y + 1 byte/pixel UV
    //   P010 packs the 10-bit value into the 10 MSBs of a uint16_t; the 6 LSBs are 0.
    //   The row pitch therefore needs to cover aligned_width * 2 bytes.
    //
    // The variable is named cuda_nv12_buffer_ for historical reasons; for the
    // motion_vector path it holds P010 data instead.
    const bool   is_p010          = (mode_ == encoder_mode::motion_vector);
    const size_t bytes_per_sample = is_p010 ? 2 : 1;
    size_t       plane_height     = aligned_height_ * 3 / 2; // Y + UV at half-res, same ratio for both formats

    spdlog::get("illixr")->debug("nvenc_encoder: Allocating {} buffer: {}x{} (total height with UV: {})",
                                 is_p010 ? "P010" : "NV12", aligned_width_, aligned_height_, plane_height);

    // First try cuMemAllocPitch
    CUresult alloc_result =
        cuMemAllocPitch(&cuda_nv12_buffer_, &cuda_nv12_pitch_, aligned_width_ * bytes_per_sample, plane_height, 16);

    if (alloc_result != CUDA_SUCCESS) {
        // If pitch allocation fails, try regular allocation with manual pitch
        spdlog::get("illixr")->warn("nvenc_encoder: cuMemAllocPitch failed, trying cuMemAlloc");

        // Use aligned pitch (multiple of 256 for optimal performance)
        cuda_nv12_pitch_  = ((aligned_width_ * bytes_per_sample + 255) / 256) * 256;
        size_t total_size = cuda_nv12_pitch_ * plane_height;

        spdlog::get("illixr")->debug("nvenc_encoder: Trying cuMemAlloc with pitch={}, total_size={}", cuda_nv12_pitch_,
                                     total_size);

        alloc_result = cuMemAlloc(&cuda_nv12_buffer_, total_size);
        if (alloc_result != CUDA_SUCCESS) {
            const char* error_name = nullptr;
            cuGetErrorName(alloc_result, &error_name);
            spdlog::get("illixr")->error("nvenc_encoder: cuMemAlloc also failed: {}",
                                         error_name ? error_name : std::to_string(alloc_result));
            throw std::runtime_error("Failed to allocate encoder input buffer");
        }
    }

    spdlog::get("illixr")->debug("nvenc_encoder: encoder input buffer allocated at {:p}, pitch={}", (void*) cuda_nv12_buffer_,
                                 cuda_nv12_pitch_);

    if (is_p010) {
        // P010 neutral: 10-bit value 512 (midpoint = zero velocity) packed into MSBs.
        // 512 << 6 = 32768 = 0x8000.  cuMemsetD16 sets each 16-bit word.
        const size_t total_words = cuda_nv12_pitch_ * plane_height / 2;
        check_cuda(cuMemsetD16(cuda_nv12_buffer_, 0x8000, total_words), "Clear P010 buffer failed");
    } else {
        // NV12 neutral: Y=16 (black in limited-range BT.709), UV=128 (neutral chroma).
        check_cuda(cuMemsetD8(cuda_nv12_buffer_, 16, cuda_nv12_pitch_ * aligned_height_), "Clear Y plane failed");
        check_cuda(
            cuMemsetD8(cuda_nv12_buffer_ + cuda_nv12_pitch_ * aligned_height_, 128, cuda_nv12_pitch_ * aligned_height_ / 2),
            "Clear UV plane failed");
    }

    // Register buffer with NVENC
    NV_ENC_REGISTER_RESOURCE register_res = {};
    register_res.version                  = nvenc_struct_version(NV_ENC_REGISTER_RESOURCE_VER);
    register_res.resourceType             = NV_ENC_INPUT_RESOURCE_TYPE_CUDADEVICEPTR;
    register_res.width                    = aligned_width_;
    register_res.height                   = aligned_height_;
    register_res.pitch                    = static_cast<uint32_t>(cuda_nv12_pitch_);
    register_res.resourceToRegister       = reinterpret_cast<void*>(cuda_nv12_buffer_);
    register_res.bufferFormat             = is_p010 ? NV_ENC_BUFFER_FORMAT_YUV420_10BIT : NV_ENC_BUFFER_FORMAT_NV12;
    register_res.bufferUsage              = NV_ENC_INPUT_IMAGE;

    check_nvenc(nvenc_.nvEncRegisterResource(encoder_, &register_res), "Register NV12 resource failed");
    registered_nv12_ = register_res.registeredResource;

    // Create output bitstream buffer
    NV_ENC_CREATE_BITSTREAM_BUFFER create_output = {};
    create_output.version                        = nvenc_struct_version(NV_ENC_CREATE_BITSTREAM_BUFFER_VER);

    check_nvenc(nvenc_.nvEncCreateBitstreamBuffer(encoder_, &create_output), "Create output buffer failed");
    output_buffer_ = create_output.bitstreamBuffer;

    spdlog::get("illixr")->info("nvenc_encoder: Buffers created, {} pitch={}", is_p010 ? "P010" : "NV12", cuda_nv12_pitch_);
}

void nvenc_encoder::get_sequence_headers() {
    // Get sequence headers.
    // HEVC:  VPS/SPS/PPS NAL units.
    // AV1:   OBU Sequence Header.
    NV_ENC_SEQUENCE_PARAM_PAYLOAD seq_params = {};
    memset(&seq_params, 0, sizeof(NV_ENC_SEQUENCE_PARAM_PAYLOAD));
    seq_params.version = nvenc_struct_version(NV_ENC_SEQUENCE_PARAM_PAYLOAD_VER);

    // Allocate a reasonable buffer upfront
    std::vector<uint8_t> buffer(1024);
    seq_params.spsppsBuffer = buffer.data();
    seq_params.inBufferSize = static_cast<uint32_t>(buffer.size());

    uint32_t actual_size            = 0;
    seq_params.outSPSPPSPayloadSize = &actual_size;

#ifdef USE_AV1
    const bool use_av1 = (codec_ == encoder_codec::av1);
#else
    constexpr bool use_av1 = false;
#endif // USE_AV1

    check_nvenc(nvenc_.nvEncGetSequenceParams(encoder_, &seq_params),
                use_av1 ? "Could not get AV1 sequence header" : "Could not get HEVC headers");
    if (actual_size > 0) {
        vps_sps_pps_.assign(buffer.begin(), buffer.begin() + actual_size);
        spdlog::get("illixr")->info("Got {} headers: {} bytes", use_av1 ? "AV1 OBU" : "HEVC VPS/SPS/PPS", actual_size);
    }
}

// ============================================================================
// Vulkan-CUDA Interop
// ============================================================================

bool nvenc_encoder::import_vulkan_memory(const vulkan_image_info& vk_image, cuda_imported_vulkan_image& imported) {
    imported.width  = vk_image.width;
    imported.height = vk_image.height;

    unsigned int num_channels;
    switch (vk_image.format) {
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_B8G8R8A8_UNORM:
        num_channels = 4;
        break;
    case VK_FORMAT_R8G8_UNORM: // RG depth format
        num_channels = 2;
        break;
    case VK_FORMAT_R16G16B16A16_SFLOAT: // Motion-vector RGBA16F
        num_channels        = 4;
        imported.is_float16 = true;
        break;
    default:
        spdlog::get("illixr")->error("nvenc_encoder: Unsupported Vulkan format: {}", static_cast<int>(vk_image.format));
        return false;
    }

    spdlog::get("illixr")->debug("nvenc_encoder: Format {} requires {} channels", static_cast<int>(vk_image.format),
                                 num_channels);

    // Validate input
    if (vk_image.memory == VK_NULL_HANDLE) {
        spdlog::get("illixr")->error("nvenc_encoder: Vulkan memory is VK_NULL_HANDLE");
        return false;
    }
    if (vk_image.memory_size == 0) {
        spdlog::get("illixr")->error("nvenc_encoder: Vulkan memory size is 0");
        return false;
    }

    spdlog::get("illixr")->debug("nvenc_encoder: Importing Vulkan memory: size={}, dimensions={}x{}", vk_image.memory_size,
                                 vk_image.width, vk_image.height);

    // Make sure CUDA context is current
    check_cuda(cuCtxSetCurrent(cu_context_), "Set CUDA context failed before import");

    // -----------------------------------------------------------------------
    // Acquire the OS handle from Vulkan, build the CUDA external-memory
    // descriptor, import it, and map as a mipmapped array.
    //
    // CUDA 13 changed CUarray and cudaArray_t into incompatible opaque types,
    // making reinterpret_cast between them undefined.  To get a proper
    // cudaArray_t we must use the CUDA Runtime external-memory API
    // (cudaImportExternalMemory / cudaExternalMemoryGetMappedMipmappedArray /
    // cudaGetMipmappedArrayLevel).  On CUDA 12 and older the Driver API path
    // (cuImportExternalMemory / cuExternalMemoryGetMappedMipmappedArray /
    // cuMipmappedArrayGetLevel) is used as before.
    //
    // CUDA_VERSION is the *outer* split so that the descriptor variable and
    // the import call are always in the same scope.
    // -----------------------------------------------------------------------

#if CUDA_VERSION >= 13000

    // --- CUDA 13+: Runtime API path -------------------------------------------

    cudaExternalMemoryHandleDesc rt_ext_mem_desc = {};
    rt_ext_mem_desc.size                         = vk_image.memory_size;
    rt_ext_mem_desc.flags                        = 0;

#    ifdef _WIN32
    if (!vk_ctx_.vkGetMemoryWin32HandleKHR) {
        spdlog::get("illixr")->error("nvenc_encoder: vkGetMemoryWin32HandleKHR not available");
        return false;
    }

    VkMemoryGetWin32HandleInfoKHR win32_handle_info = {};
    win32_handle_info.sType                         = VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR;
    win32_handle_info.memory                        = vk_image.memory;
    win32_handle_info.handleType                    = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;

    VkResult vk_result = vk_ctx_.vkGetMemoryWin32HandleKHR(vk_ctx_.device, &win32_handle_info, &imported.handle);

    rt_ext_mem_desc.type                = cudaExternalMemoryHandleTypeOpaqueWin32;
    rt_ext_mem_desc.handle.win32.handle = imported.handle;
    rt_ext_mem_desc.handle.win32.name   = nullptr;

    if (vk_result != VK_SUCCESS || imported.handle == nullptr || imported.handle == INVALID_HANDLE_VALUE) {
        spdlog::get("illixr")->warn("nvenc_encoder: OPAQUE_WIN32 failed ({}), trying OPAQUE_WIN32_KMT",
                                    static_cast<int>(vk_result));
        win32_handle_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_KMT_BIT;
        vk_result                    = vk_ctx_.vkGetMemoryWin32HandleKHR(vk_ctx_.device, &win32_handle_info, &imported.handle);
        if (vk_result != VK_SUCCESS) {
            spdlog::get("illixr")->error("nvenc_encoder: Failed to get Win32 handle with both types: {}",
                                         static_cast<int>(vk_result));
            return false;
        }
        rt_ext_mem_desc.type                = cudaExternalMemoryHandleTypeOpaqueWin32Kmt;
        rt_ext_mem_desc.handle.win32.handle = imported.handle;
    }

    if (imported.handle == nullptr || imported.handle == INVALID_HANDLE_VALUE) {
        spdlog::get("illixr")->error("nvenc_encoder: Got invalid Win32 handle");
        return false;
    }
    spdlog::get("illixr")->debug("nvenc_encoder: Got Win32 handle: {:p}", imported.handle);

#    else  // !_WIN32
    if (!vk_ctx_.vkGetMemoryFdKHR) {
        spdlog::get("illixr")->error("nvenc_encoder: vkGetMemoryFdKHR not available");
        return false;
    }

    VkMemoryGetFdInfoKHR fd_info = {};
    fd_info.sType                = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
    fd_info.memory               = vk_image.memory;
    fd_info.handleType           = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

    VkResult vk_result = vk_ctx_.vkGetMemoryFdKHR(vk_ctx_.device, &fd_info, &imported.fd);
    if (vk_result != VK_SUCCESS || imported.fd < 0) {
        spdlog::get("illixr")->error("nvenc_encoder: Failed to get FD: {}", static_cast<int>(vk_result));
        return false;
    }

    rt_ext_mem_desc.type      = cudaExternalMemoryHandleTypeOpaqueFd;
    rt_ext_mem_desc.handle.fd = imported.fd;
    spdlog::get("illixr")->debug("nvenc_encoder: Got FD: {}", imported.fd);
#    endif // _WIN32

    spdlog::get("illixr")->debug("nvenc_encoder: Calling cudaImportExternalMemory (runtime path) with size={}",
                                 vk_image.memory_size);

    cudaError_t rt_result = cudaImportExternalMemory(&imported.rt_ext_memory, &rt_ext_mem_desc);
    if (rt_result != cudaSuccess) {
        spdlog::get("illixr")->error("nvenc_encoder: cudaImportExternalMemory failed: {} ({})", cudaGetErrorString(rt_result),
                                     static_cast<int>(rt_result));
        spdlog::get("illixr")->error("  Memory size: {}", vk_image.memory_size);
        spdlog::get("illixr")->error("  Image dimensions: {}x{}", vk_image.width, vk_image.height);
        spdlog::get("illixr")->error("  Image format: {}", static_cast<int>(vk_image.format));
        spdlog::get("illixr")->error("  Image tiling: {}", static_cast<int>(vk_image.tiling));
        return false;
    }

    // Map as CUDA mipmapped array via Runtime API
    cudaExternalMemoryMipmappedArrayDesc rt_mip_desc = {};
    rt_mip_desc.offset                               = vk_image.memory_offset;
    rt_mip_desc.extent                               = make_cudaExtent(vk_image.width, vk_image.height, 0);
    rt_mip_desc.flags                                = 0;
    rt_mip_desc.numLevels                            = 1;

    if (num_channels == 4 && imported.is_float16) {
        // RGBA16F: 4 × 16-bit float components
        rt_mip_desc.formatDesc = cudaCreateChannelDesc(16, 16, 16, 16, cudaChannelFormatKindFloat);
    } else if (num_channels == 4) {
        rt_mip_desc.formatDesc = cudaCreateChannelDesc<uchar4>(); // BGRA
    } else if (num_channels == 2) {
        rt_mip_desc.formatDesc = cudaCreateChannelDesc<uchar2>(); // RG
    } else {
        spdlog::get("illixr")->error("nvenc_encoder: Unsupported channel count: {}", num_channels);
        cudaDestroyExternalMemory(imported.rt_ext_memory);
        return false;
    }

    rt_result = cudaExternalMemoryGetMappedMipmappedArray(&imported.rt_mipmap, imported.rt_ext_memory, &rt_mip_desc);
    if (rt_result != cudaSuccess) {
        spdlog::get("illixr")->error("nvenc_encoder: cudaExternalMemoryGetMappedMipmappedArray failed: {}",
                                     cudaGetErrorString(rt_result));
        cudaDestroyExternalMemory(imported.rt_ext_memory);
        imported.rt_ext_memory = nullptr;
        return false;
    }

    rt_result = cudaGetMipmappedArrayLevel(&imported.rt_array, imported.rt_mipmap, 0);
    if (rt_result != cudaSuccess) {
        spdlog::get("illixr")->error("nvenc_encoder: cudaGetMipmappedArrayLevel failed: {}", cudaGetErrorString(rt_result));
        cudaFreeMipmappedArray(imported.rt_mipmap);
        imported.rt_mipmap = nullptr;
        cudaDestroyExternalMemory(imported.rt_ext_memory);
        imported.rt_ext_memory = nullptr;
        return false;
    }

    // Log array info using the Runtime API (cudaArrayGetInfo, CUDA 13+)
    {
        cudaChannelFormatDesc format_desc = {};
        cudaExtent            extent      = {};
        unsigned int          array_flags = 0;
        if (cudaArrayGetInfo(&format_desc, &extent, &array_flags, imported.rt_array) == cudaSuccess) {
            spdlog::get("illixr")->info(
                "nvenc_encoder: CUDA array created (runtime) - {}x{}, channel bits={}/{}/{}/{}, flags=0x{:X}", extent.width,
                extent.height, format_desc.x, format_desc.y, format_desc.z, format_desc.w, array_flags);
        } else {
            spdlog::get("illixr")->warn("nvenc_encoder: Failed to query array descriptor (runtime)");
        }
    }

#else // CUDA_VERSION < 13000 — Driver API path

    CUDA_EXTERNAL_MEMORY_HANDLE_DESC ext_mem_desc = {};
    memset(&ext_mem_desc, 0, sizeof(ext_mem_desc));
    ext_mem_desc.size  = vk_image.memory_size;
    ext_mem_desc.flags = 0;

#    ifdef _WIN32
    if (!vk_ctx_.vkGetMemoryWin32HandleKHR) {
        spdlog::get("illixr")->error("nvenc_encoder: vkGetMemoryWin32HandleKHR not available");
        return false;
    }

    VkMemoryGetWin32HandleInfoKHR win32_handle_info = {};
    win32_handle_info.sType                         = VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR;
    win32_handle_info.memory                        = vk_image.memory;
    win32_handle_info.handleType                    = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;

    VkResult vk_result = vk_ctx_.vkGetMemoryWin32HandleKHR(vk_ctx_.device, &win32_handle_info, &imported.handle);
    CUexternalMemoryHandleType cuda_handle_type = CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32;

    if (vk_result != VK_SUCCESS || imported.handle == nullptr || imported.handle == INVALID_HANDLE_VALUE) {
        spdlog::get("illixr")->warn("nvenc_encoder: OPAQUE_WIN32 failed ({}), trying OPAQUE_WIN32_KMT",
                                    static_cast<int>(vk_result));
        win32_handle_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_KMT_BIT;
        vk_result                    = vk_ctx_.vkGetMemoryWin32HandleKHR(vk_ctx_.device, &win32_handle_info, &imported.handle);
        cuda_handle_type             = CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_KMT;
        if (vk_result != VK_SUCCESS) {
            spdlog::get("illixr")->error("nvenc_encoder: Failed to get Win32 handle with both types: {}",
                                         static_cast<int>(vk_result));
            return false;
        }
    }

    if (imported.handle == nullptr || imported.handle == INVALID_HANDLE_VALUE) {
        spdlog::get("illixr")->error("nvenc_encoder: Got invalid Win32 handle");
        return false;
    }
    spdlog::get("illixr")->debug("nvenc_encoder: Got Win32 handle: {:p}, type: {}", imported.handle,
                                 cuda_handle_type == CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32 ? "OPAQUE_WIN32"
                                                                                                 : "OPAQUE_WIN32_KMT");

    ext_mem_desc.type                = cuda_handle_type;
    ext_mem_desc.handle.win32.handle = imported.handle;
    ext_mem_desc.handle.win32.name   = nullptr;

#    else  // !_WIN32
    if (!vk_ctx_.vkGetMemoryFdKHR) {
        spdlog::get("illixr")->error("nvenc_encoder: vkGetMemoryFdKHR not available");
        return false;
    }

    VkMemoryGetFdInfoKHR fd_info = {};
    fd_info.sType                = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
    fd_info.memory               = vk_image.memory;
    fd_info.handleType           = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

    VkResult vk_result = vk_ctx_.vkGetMemoryFdKHR(vk_ctx_.device, &fd_info, &imported.fd);
    if (vk_result != VK_SUCCESS || imported.fd < 0) {
        spdlog::get("illixr")->error("nvenc_encoder: Failed to get FD: {}", static_cast<int>(vk_result));
        return false;
    }
    spdlog::get("illixr")->debug("nvenc_encoder: Got FD: {}", imported.fd);

    ext_mem_desc.type      = CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD;
    ext_mem_desc.handle.fd = imported.fd;
#    endif // _WIN32

    spdlog::get("illixr")->debug("nvenc_encoder: Calling cuImportExternalMemory (driver path) with size={}", ext_mem_desc.size);

    CUresult cu_result = cuImportExternalMemory(&imported.ext_memory, &ext_mem_desc);
    if (cu_result != CUDA_SUCCESS) {
        const char* error_name = nullptr;
        cuGetErrorName(cu_result, &error_name);
        spdlog::get("illixr")->error("nvenc_encoder: Failed to import external memory: {} ({})",
                                     error_name ? error_name : "unknown", static_cast<int>(cu_result));
        spdlog::get("illixr")->error("  Memory size: {}", vk_image.memory_size);
        spdlog::get("illixr")->error("  Image dimensions: {}x{}", vk_image.width, vk_image.height);
        spdlog::get("illixr")->error("  Image format: {}", static_cast<int>(vk_image.format));
        spdlog::get("illixr")->error("  Image tiling: {}", static_cast<int>(vk_image.tiling));
        return false;
    }

    CUDA_EXTERNAL_MEMORY_MIPMAPPED_ARRAY_DESC mipmap_desc = {};
    mipmap_desc.offset                                    = vk_image.memory_offset;
    mipmap_desc.arrayDesc.Width                           = vk_image.width;
    mipmap_desc.arrayDesc.Height                          = vk_image.height;
    mipmap_desc.arrayDesc.Depth                           = 0;
    mipmap_desc.arrayDesc.Format      = imported.is_float16 ? CU_AD_FORMAT_HALF : CU_AD_FORMAT_UNSIGNED_INT8;
    mipmap_desc.arrayDesc.NumChannels = num_channels;
    mipmap_desc.arrayDesc.Flags       = 0;
    mipmap_desc.numLevels             = 1;

    cu_result = cuExternalMemoryGetMappedMipmappedArray(&imported.mipmap, imported.ext_memory, &mipmap_desc);
    if (cu_result != CUDA_SUCCESS) {
        spdlog::get("illixr")->error("nvenc_encoder: Failed to get mipmapped array: {}", static_cast<int>(cu_result));
        cuDestroyExternalMemory(imported.ext_memory);
        imported.ext_memory = nullptr;
        return false;
    }

    cu_result = cuMipmappedArrayGetLevel(&imported.array, imported.mipmap, 0);
    if (cu_result != CUDA_SUCCESS) {
        spdlog::get("illixr")->error("nvenc_encoder: Failed to get array level: {}", static_cast<int>(cu_result));
        cuMipmappedArrayDestroy(imported.mipmap);
        imported.mipmap = nullptr;
        cuDestroyExternalMemory(imported.ext_memory);
        imported.ext_memory = nullptr;
        return false;
    }

    // Log array info using the Driver API
    {
        CUDA_ARRAY3D_DESCRIPTOR array_desc = {};
        cu_result                          = cuArray3DGetDescriptor(&array_desc, imported.array);
        if (cu_result == CUDA_SUCCESS) {
            const char* format_str;
            switch (array_desc.Format) {
            case CU_AD_FORMAT_UNSIGNED_INT8:
                format_str = "UINT8";
                break;
            case CU_AD_FORMAT_UNSIGNED_INT16:
                format_str = "UINT16";
                break;
            case CU_AD_FORMAT_UNSIGNED_INT32:
                format_str = "UINT32";
                break;
            case CU_AD_FORMAT_SIGNED_INT8:
                format_str = "SINT8";
                break;
            case CU_AD_FORMAT_SIGNED_INT16:
                format_str = "SINT16";
                break;
            case CU_AD_FORMAT_SIGNED_INT32:
                format_str = "SINT32";
                break;
            case CU_AD_FORMAT_HALF:
                format_str = "HALF";
                break;
            case CU_AD_FORMAT_FLOAT:
                format_str = "FLOAT";
                break;
            default:
                format_str = "UNKNOWN";
                break;
            }

            spdlog::get("illixr")->info(
                "nvenc_encoder: CUDA array created (driver) - {}x{}, format={}, channels={}, flags=0x{:X}", array_desc.Width,
                array_desc.Height, format_str, array_desc.NumChannels, array_desc.Flags);
        } else {
            spdlog::get("illixr")->warn("nvenc_encoder: Failed to query array descriptor");
        }
    }

#endif // CUDA_VERSION >= 13000

    // Create a texture object for efficient GPU read
    if (!create_texture_object(imported)) {
        spdlog::get("illixr")->error("nvenc_encoder: Failed to create texture object");
#if CUDA_VERSION >= 13000
        cudaFreeMipmappedArray(imported.rt_mipmap);
        imported.rt_mipmap = nullptr;
        cudaDestroyExternalMemory(imported.rt_ext_memory);
        imported.rt_ext_memory = nullptr;
#else
        cuMipmappedArrayDestroy(imported.mipmap);
        imported.mipmap = nullptr;
        cuDestroyExternalMemory(imported.ext_memory);
        imported.ext_memory = nullptr;
#endif
        return false;
    }

    imported.valid = true;
    return true;
}

bool nvenc_encoder::create_texture_object(cuda_imported_vulkan_image& imported) {
    // Create texture descriptor
    cudaResourceDesc res_desc = {};
    res_desc.resType          = cudaResourceTypeArray;

    // CUDA 13 made CUarray and cudaArray_t incompatible opaque types, so the
    // reinterpret_cast used in the driver-API path is no longer valid.  On
    // CUDA 13+ we imported via the Runtime API and rt_array is already
    // cudaArray_t; on CUDA 12 and older we cast the driver-API CUarray.
#if CUDA_VERSION >= 13000
    res_desc.res.array.array = imported.rt_array;
#else
    res_desc.res.array.array = reinterpret_cast<cudaArray_t>(imported.array);
#endif

    cudaTextureDesc tex_desc = {};
    tex_desc.addressMode[0]  = cudaAddressModeClamp;
    tex_desc.addressMode[1]  = cudaAddressModeClamp;
    // Use bilinear filtering with normalized coordinates so the scaled kernel can
    // sample at (u, v) in [0,1] regardless of the source-to-target resolution ratio.
    // This also gives higher quality when downsampling (vs point sampling).
    tex_desc.filterMode = cudaFilterModeLinear;
    // cudaReadModeNormalizedFloat converts integer data to [0,1] floats and is
    // invalid for floating-point channel formats.  Use cudaReadModeElementType
    // for float16 sources so tex2D<float4> returns the raw half values as floats.
    tex_desc.readMode         = imported.is_float16 ? cudaReadModeElementType : cudaReadModeNormalizedFloat;
    tex_desc.normalizedCoords = 1; // Use normalized [0,1] UV coords

    cudaError_t err = cudaCreateTextureObject(&imported.texture, &res_desc, &tex_desc, nullptr);
    if (err != cudaSuccess) {
        spdlog::get("illixr")->error("nvenc_encoder: cudaCreateTextureObject failed: {}", cudaGetErrorString(err));
        return false;
    }

    return true;
}

void nvenc_encoder::release_imported_image(cuda_imported_vulkan_image& imported) {
    if (imported.texture) {
        cudaDestroyTextureObject(imported.texture);
        imported.texture = 0;
    }

    if (imported.surface) {
        cudaDestroySurfaceObject(imported.surface);
        imported.surface = 0;
    }

    // Release the array / mipmapped array / external memory using whichever
    // API was used during import (Runtime on CUDA 13+, Driver on CUDA 12-).
#if CUDA_VERSION >= 13000
    // cudaArray_t is managed by the mipmapped array; no separate free needed.
    imported.rt_array = nullptr;

    if (imported.rt_mipmap) {
        cudaFreeMipmappedArray(imported.rt_mipmap);
        imported.rt_mipmap = nullptr;
    }

    if (imported.rt_ext_memory) {
        cudaDestroyExternalMemory(imported.rt_ext_memory);
        imported.rt_ext_memory = nullptr;
    }
#else
    // Driver API: no explicit free for array (owned by the mipmap).
    imported.array = nullptr;

    if (imported.mipmap) {
        cuMipmappedArrayDestroy(imported.mipmap);
        imported.mipmap = nullptr;
    }

    if (imported.ext_memory) {
        cuDestroyExternalMemory(imported.ext_memory);
        imported.ext_memory = nullptr;
    }
#endif // CUDA_VERSION >= 13000

#ifdef _WIN32
    if (imported.handle) {
        CloseHandle(imported.handle);
        imported.handle = nullptr;
    }
#else
    if (imported.fd >= 0) {
        close(imported.fd);
        imported.fd = -1;
    }
#endif

    imported.valid = false;
}

int nvenc_encoder::import_vulkan_image(const vulkan_image_info& vk_image) {
    cuda_imported_vulkan_image imported;
    if (!import_vulkan_memory(vk_image, imported)) {
        return -1;
    }

    imported_images_.push_back(std::move(imported));
    return static_cast<int>(imported_images_.size() - 1);
}

// ============================================================================
// GPU Color Conversion
// ============================================================================

void nvenc_encoder::convert_bgra_to_nv12_gpu(const cuda_imported_vulkan_image& imported) {
    // Clear entire NV12 buffer to black BEFORE conversion
    // This eliminates triangle artifacts in padding regions
    size_t y_plane_size  = cuda_nv12_pitch_ * aligned_height_;
    size_t uv_plane_size = cuda_nv12_pitch_ * (aligned_height_ / 2);

    if (mode_ == encoder_mode::depth) {
        // For depth: clear to neutral values.
        // Y=128 (middle gray), UV=128 (neutral chroma)
        cudaMemsetAsync(reinterpret_cast<void*>(cuda_nv12_buffer_), 128, y_plane_size, cu_stream_);
        cudaMemsetAsync(reinterpret_cast<void*>(cuda_nv12_buffer_ + y_plane_size), 128, uv_plane_size, cu_stream_);

        // Use RG-to-NV12 kernel (preserves depth bytes)
        cudaError_t err = launch_rg_depth_to_nv12_scaled(imported.texture, reinterpret_cast<uint8_t*>(cuda_nv12_buffer_),
                                                         cuda_nv12_pitch_, width_, height_, aligned_height_, cu_stream_);

        if (err != cudaSuccess) {
            spdlog::get("illixr")->error("nvenc_encoder: RG depth conversion failed: {}", cudaGetErrorString(err));
            throw std::runtime_error("GPU depth conversion failed");
        }
    } else if (mode_ == encoder_mode::motion_vector) {
        // For motion vectors: neutral is zero velocity → 10-bit midpoint 512 = 0x8000.
        const size_t total_words = cuda_nv12_pitch_ * aligned_height_ * 3 / 2 / 2;
        cudaMemsetAsync(reinterpret_cast<void*>(cuda_nv12_buffer_), 0, // filled below, just clear first
                        cuda_nv12_pitch_ * aligned_height_ * 3 / 2, cu_stream_);

        // RGBA16F → P010: Vx→Y (10-bit), Vy→U (10-bit), Vz→V (10-bit), normalised to [0,1023].
        cudaError_t err = launch_rgba16f_to_p010_scaled(imported.texture, reinterpret_cast<uint16_t*>(cuda_nv12_buffer_),
                                                        cuda_nv12_pitch_, // byte pitch — kernel divides by 2 internally
                                                        width_, height_, aligned_height_, cu_stream_);

        if (err != cudaSuccess) {
            spdlog::get("illixr")->error("nvenc_encoder: RGBA16F→P010 motion-vector conversion failed: {}",
                                         cudaGetErrorString(err));
            throw std::runtime_error("GPU motion-vector P010 conversion failed");
        }
    } else {
        // COLOR: clear Y to 16 (black in limited-range YUV), UV to 128 (neutral chroma).
        cudaMemsetAsync(reinterpret_cast<void*>(cuda_nv12_buffer_), 16, y_plane_size, cu_stream_);
        cudaMemsetAsync(reinterpret_cast<void*>(cuda_nv12_buffer_ + y_plane_size), 128, uv_plane_size, cu_stream_);

        // Use the scaled kernel with normalized-coordinate texture.
        // Iterates over width_ x height_ OUTPUT pixels and samples the texture at normalized
        // UV coordinates, so it correctly handles any source-to-target ratio (including 1:1).
        // When Monado renders at >100% scale, imported.width/height > width_/height_ and this
        // performs a bilinear downsample on the GPU with no extra passes or allocations.
        cudaError_t err = launch_bgra_texture_to_nv12_scaled(imported.texture, reinterpret_cast<uint8_t*>(cuda_nv12_buffer_),
                                                             cuda_nv12_pitch_, width_, height_, aligned_height_, cu_stream_);
        // launch_bgra_texture_to_nv12(imported.texture, reinterpret_cast<uint8_t*>(cuda_nv12_buffer_), cuda_nv12_pitch_,
        //                             imported.width, imported.height, aligned_height_, cu_stream_);

        if (err != cudaSuccess) {
            spdlog::get("illixr")->error("nvenc_encoder: Color conversion kernel failed: {}", cudaGetErrorString(err));
            throw std::runtime_error("GPU color conversion failed");
        }
    }

    // Synchronize to ensure the conversion is complete before encoding
    cudaError_t err = cudaStreamSynchronize(cu_stream_);
    if (err != cudaSuccess) {
        spdlog::get("illixr")->error("nvenc_encoder: Stream sync failed: {}", cudaGetErrorString(err));
        throw std::runtime_error("Stream synchronization failed");
    }
}

// ============================================================================
// Encoding
// ============================================================================

std::vector<uint8_t> nvenc_encoder::encode(int imported_index) {
    std::lock_guard<std::mutex> lock(encode_mutex_);

    if (!initialized_.load()) {
        throw std::runtime_error("Encoder not initialized");
    }

    if (imported_index < 0 || imported_index >= static_cast<int>(imported_images_.size())) {
        throw std::runtime_error("Invalid imported image index");
    }

    auto& imported = imported_images_[imported_index];
    if (!imported.valid) {
        throw std::runtime_error("Imported image not valid");
    }

    check_cuda(cuCtxSetCurrent(cu_context_), "Set CUDA context failed");

#ifdef DUMP_FRAMES
    frame_saver_->increment_frame_count();
    // Save frame to disk if enabled (before color conversion)
    if (frame_saver_ && frame_saver_->will_save_next()) {
        spdlog::get("illixr")->info("Saving frame");
        save_cuda_frame_to_disk(imported);
    }
#endif
    // Convert BGRA to NV12 on GPU
    convert_bgra_to_nv12_gpu(imported);

    // Map the registered resource
    NV_ENC_MAP_INPUT_RESOURCE map_resource = {};
    map_resource.version                   = nvenc_struct_version(NV_ENC_MAP_INPUT_RESOURCE_VER);
    map_resource.registeredResource        = registered_nv12_;

    check_nvenc(nvenc_.nvEncMapInputResource(encoder_, &map_resource), "Map input resource failed");

    // Encode
    NV_ENC_PIC_PARAMS pic_params = {};
    pic_params.version           = nvenc_struct_version(NV_ENC_PIC_PARAMS_VER);
    pic_params.inputBuffer       = map_resource.mappedResource;
    pic_params.bufferFmt         = map_resource.mappedBufferFmt;
    pic_params.inputWidth        = aligned_width_;
    pic_params.inputHeight       = aligned_height_;
    pic_params.outputBitstream   = output_buffer_;
    pic_params.pictureStruct     = NV_ENC_PIC_STRUCT_FRAME;
    pic_params.inputTimeStamp    = frame_count_++;
    pic_params.encodePicFlags    = 0; // Normal P-frame by default
    if (pending_idrs_ > 0) {
#ifdef USE_AV1
        // For AV1: use FORCEIDR (valid in NVENC SDK 12+) which produces a true
        // AV1 KEY_FRAME that resets the decoder reference buffer and triggers
        // repeatSeqHdr to inject the OBU Sequence Header automatically.
        // FORCEINTRA only produces an INTRA_ONLY_FRAME which does NOT reset
        // references or trigger header injection, causing the decoder to lose
        // sync at every GOP boundary after the startup IDRs.
        // Do NOT add OUTPUT_SPSPPS — it is a no-op for AV1 and can corrupt output.
        const bool use_av1 = (codec_ == encoder_codec::av1);
        pic_params.encodePicFlags =
            use_av1 ? NV_ENC_PIC_FLAG_FORCEIDR : (NV_ENC_PIC_FLAG_FORCEIDR | NV_ENC_PIC_FLAG_OUTPUT_SPSPPS);
#else
        pic_params.encodePicFlags = NV_ENC_PIC_FLAG_FORCEIDR | NV_ENC_PIC_FLAG_OUTPUT_SPSPPS;
#endif // USE_AV1
        pending_idrs_--;
    }

    check_nvenc(nvenc_.nvEncEncodePicture(encoder_, &pic_params), "Encode picture failed");

    // Unmap resource
    check_nvenc(nvenc_.nvEncUnmapInputResource(encoder_, map_resource.mappedResource), "Unmap resource failed");

    // Lock and copy bitstream
    NV_ENC_LOCK_BITSTREAM lock_bitstream = {};
    lock_bitstream.version               = nvenc_struct_version(NV_ENC_LOCK_BITSTREAM_VER);
    lock_bitstream.outputBitstream       = output_buffer_;

    check_nvenc(nvenc_.nvEncLockBitstream(encoder_, &lock_bitstream), "Lock bitstream failed");

    std::vector<uint8_t> encoded_data;
    const auto           bitstream_data = static_cast<uint8_t*>(lock_bitstream.bitstreamBufferPtr);
    const size_t         bitstream_size = lock_bitstream.bitstreamSizeInBytes;

    // Determine whether this is a keyframe.
    // For HEVC the encoder produces NV_ENC_PIC_TYPE_IDR for forced and auto-GOP keyframes.
    // For AV1  the encoder may produce NV_ENC_PIC_TYPE_IDR (forced) or NV_ENC_PIC_TYPE_I
    // (auto-GOP intra) — both are true AV1 KEY_FRAMEs that reset decoder references.
    // Treating only IDR as a keyframe causes the decoder to lose the
    // AMEDIACODEC_BUFFER_FLAG_KEY_FRAME hint on auto-GOP boundaries, which makes
    // MediaCodec's internal error-recovery heuristics misfire.
    const bool is_keyframe =
        (lock_bitstream.pictureType == NV_ENC_PIC_TYPE_IDR) || (lock_bitstream.pictureType == NV_ENC_PIC_TYPE_I);
    last_frame_was_keyframe_ = is_keyframe;

    // For HEVC: prepend VPS/SPS/PPS to IDR frames (NVENC does not do this
    // automatically unless OUTPUT_SPSPPS is set per-frame).
    // For AV1:  repeatSeqHdr=1 causes NVENC to embed the OBU Sequence Header
    // inside every keyframe bitstream automatically, so prepending vps_sps_pps_
    // again would double it and produce an invalid bitstream.
#ifdef USE_AV1
    const bool prepend_headers = (codec_ != encoder_codec::av1) && is_keyframe && !vps_sps_pps_.empty();
#else
    const bool prepend_headers = is_keyframe && !vps_sps_pps_.empty();
#endif // USE_AV1

    if (prepend_headers) {
        encoded_data.reserve(vps_sps_pps_.size() + bitstream_size);
        encoded_data.insert(encoded_data.end(), vps_sps_pps_.begin(), vps_sps_pps_.end());
    }

    encoded_data.insert(encoded_data.end(), bitstream_data, bitstream_data + bitstream_size);

    check_nvenc(nvenc_.nvEncUnlockBitstream(encoder_, output_buffer_), "Unlock bitstream failed");

    return encoded_data;
}

std::vector<uint8_t> nvenc_encoder::encode_image(const vulkan_image_info& vk_image) {
    int idx = import_vulkan_image(vk_image);
    if (idx < 0) {
        throw std::runtime_error("Failed to import Vulkan image");
    }

    auto result = encode(idx);

    // Release the temporarily imported image
    release_imported_image(imported_images_[idx]);
    imported_images_.pop_back();

    return result;
}

#ifdef COMBINED_ENCODING

// ============================================================================
// Combined Stereo Encoding
// ============================================================================

void nvenc_encoder::convert_stereo_to_nv12_gpu(const cuda_imported_vulkan_image& left,
                                               const cuda_imported_vulkan_image& right) {
    // Clear the combined NV12 buffer before conversion.
    // Y = 16  (black in limited-range BT.709)
    // UV = 128 (neutral chroma)
    // The buffer covers the full combined width (aligned_width_ = aligned per-eye width * 2).
    const size_t y_plane_size  = cuda_nv12_pitch_ * aligned_height_;
    const size_t uv_plane_size = cuda_nv12_pitch_ * (aligned_height_ / 2);
    cudaMemsetAsync(reinterpret_cast<void*>(cuda_nv12_buffer_), 16, y_plane_size, cu_stream_);
    cudaMemsetAsync(reinterpret_cast<void*>(cuda_nv12_buffer_ + y_plane_size), 128, uv_plane_size, cu_stream_);

    // width_ is the combined encode width (per_eye_width * 2) because the encoder
    // was constructed with the full combined dimension.  The stereo kernel expects
    // the per-eye width as dst_eye_width so it can compute normalized coordinates
    // within each eye's half independently.
    cudaError_t err =
        launch_bgra_stereo_to_nv12(left.texture, right.texture, reinterpret_cast<uint8_t*>(cuda_nv12_buffer_), cuda_nv12_pitch_,
                                   width_ / 2, // per-eye encode width = combined width / 2
                                   height_,    // encode height
                                   aligned_height_, cu_stream_);

    if (err != cudaSuccess) {
        spdlog::get("illixr")->error("nvenc_encoder: Stereo blit kernel failed: {}", cudaGetErrorString(err));
        throw std::runtime_error("GPU stereo color conversion failed");
    }

    cudaError_t sync_err = cudaStreamSynchronize(cu_stream_);
    if (sync_err != cudaSuccess) {
        spdlog::get("illixr")->error("nvenc_encoder: Stream sync failed after stereo blit: {}", cudaGetErrorString(sync_err));
        throw std::runtime_error("Stream synchronization failed");
    }
}

void nvenc_encoder::ensure_rgba_input_buffers(uint32_t source_width, uint32_t source_height) {
    if (source_width == 0 || source_height == 0) {
        throw std::invalid_argument("RGBA source dimensions must be non-zero");
    }
    if (cuda_left_rgba_buffer_ != 0 && cuda_right_rgba_buffer_ != 0 && rgba_source_width_ == source_width &&
        rgba_source_height_ == source_height) {
        return;
    }

    if (cuda_left_rgba_buffer_ != 0) {
        check_cuda(cuMemFree(cuda_left_rgba_buffer_), "Free old left RGBA input buffer failed");
        cuda_left_rgba_buffer_ = 0;
    }
    if (cuda_right_rgba_buffer_ != 0) {
        check_cuda(cuMemFree(cuda_right_rgba_buffer_), "Free old right RGBA input buffer failed");
        cuda_right_rgba_buffer_ = 0;
    }

    check_cuda(cuMemAllocPitch(&cuda_left_rgba_buffer_, &cuda_left_rgba_pitch_,
                               static_cast<size_t>(source_width) * 4, source_height, 16),
               "Allocate left RGBA input buffer failed");
    try {
        check_cuda(cuMemAllocPitch(&cuda_right_rgba_buffer_, &cuda_right_rgba_pitch_,
                                   static_cast<size_t>(source_width) * 4, source_height, 16),
                   "Allocate right RGBA input buffer failed");
    } catch (...) {
        cuMemFree(cuda_left_rgba_buffer_);
        cuda_left_rgba_buffer_ = 0;
        throw;
    }
    rgba_source_width_  = source_width;
    rgba_source_height_ = source_height;
    spdlog::get("illixr")->info("nvenc_encoder: allocated persistent RGBA input buffers for {}x{} eye images",
                                 source_width, source_height);
}

void nvenc_encoder::convert_rgba_stereo_to_nv12_gpu(const uint8_t* left_rgba, size_t left_pitch,
                                                    const uint8_t* right_rgba, size_t right_pitch,
                                                    uint32_t source_width, uint32_t source_height, bool flip_y) {
    if (left_rgba == nullptr || right_rgba == nullptr || left_pitch < static_cast<size_t>(source_width) * 4 ||
        right_pitch < static_cast<size_t>(source_width) * 4) {
        throw std::invalid_argument("Invalid RGBA stereo input pointer or row pitch");
    }
    ensure_rgba_input_buffers(source_width, source_height);

    check_cuda_runtime(cudaMemcpy2DAsync(reinterpret_cast<void*>(cuda_left_rgba_buffer_), cuda_left_rgba_pitch_, left_rgba,
                                         left_pitch, static_cast<size_t>(source_width) * 4, source_height,
                                         cudaMemcpyHostToDevice, cu_stream_),
                       "Copy left RGBA input to CUDA failed");
    check_cuda_runtime(cudaMemcpy2DAsync(reinterpret_cast<void*>(cuda_right_rgba_buffer_), cuda_right_rgba_pitch_, right_rgba,
                                         right_pitch, static_cast<size_t>(source_width) * 4, source_height,
                                         cudaMemcpyHostToDevice, cu_stream_),
                       "Copy right RGBA input to CUDA failed");

    const size_t y_plane_size  = cuda_nv12_pitch_ * aligned_height_;
    const size_t uv_plane_size = cuda_nv12_pitch_ * (aligned_height_ / 2);
    check_cuda_runtime(cudaMemsetAsync(reinterpret_cast<void*>(cuda_nv12_buffer_), 16, y_plane_size, cu_stream_),
                       "Clear RGBA stereo Y plane failed");
    check_cuda_runtime(cudaMemsetAsync(reinterpret_cast<void*>(cuda_nv12_buffer_ + y_plane_size), 128, uv_plane_size,
                                       cu_stream_),
                       "Clear RGBA stereo UV plane failed");

    const cudaError_t conversion = launch_rgba_stereo_linear_to_nv12(
        reinterpret_cast<const uint8_t*>(cuda_left_rgba_buffer_), cuda_left_rgba_pitch_,
        reinterpret_cast<const uint8_t*>(cuda_right_rgba_buffer_), cuda_right_rgba_pitch_, source_width, source_height,
        reinterpret_cast<uint8_t*>(cuda_nv12_buffer_), cuda_nv12_pitch_, width_ / 2, height_, aligned_height_, flip_y,
        cu_stream_);
    check_cuda_runtime(conversion, "RGBA stereo conversion kernel failed");
    check_cuda_runtime(cudaStreamSynchronize(cu_stream_), "RGBA stereo conversion synchronization failed");
}

std::vector<uint8_t> nvenc_encoder::encode_stereo(int left_index, int right_index) {
    std::lock_guard<std::mutex> lock(encode_mutex_);

    if (!initialized_.load()) {
        throw std::runtime_error("Encoder not initialized");
    }
    if (left_index < 0 || left_index >= static_cast<int>(imported_images_.size()) || right_index < 0 ||
        right_index >= static_cast<int>(imported_images_.size())) {
        throw std::runtime_error("Invalid imported image index in encode_stereo");
    }

    const auto& left  = imported_images_[left_index];
    const auto& right = imported_images_[right_index];
    if (!left.valid || !right.valid) {
        throw std::runtime_error("Imported image not valid in encode_stereo");
    }

    check_cuda(cuCtxSetCurrent(cu_context_), "Set CUDA context failed");

    // Blit both eyes into the combined NV12 buffer in one kernel launch.
    convert_stereo_to_nv12_gpu(left, right);

    // Map, encode, unmap — identical to encode().
    NV_ENC_MAP_INPUT_RESOURCE map_resource = {};
    map_resource.version                   = nvenc_struct_version(NV_ENC_MAP_INPUT_RESOURCE_VER);
    map_resource.registeredResource        = registered_nv12_;
    check_nvenc(nvenc_.nvEncMapInputResource(encoder_, &map_resource), "Map input resource failed (encode_stereo)");

    NV_ENC_PIC_PARAMS pic_params = {};
    pic_params.version           = nvenc_struct_version(NV_ENC_PIC_PARAMS_VER);
    pic_params.inputBuffer       = map_resource.mappedResource;
    pic_params.bufferFmt         = map_resource.mappedBufferFmt;
    pic_params.inputWidth        = aligned_width_;
    pic_params.inputHeight       = aligned_height_;
    pic_params.outputBitstream   = output_buffer_;
    pic_params.pictureStruct     = NV_ENC_PIC_STRUCT_FRAME;
    pic_params.inputTimeStamp    = frame_count_++;
    pic_params.encodePicFlags    = 0;
    if (pending_idrs_ > 0) {
#    ifdef USE_AV1
        const bool use_av1 = (codec_ == encoder_codec::av1);
        pic_params.encodePicFlags =
            use_av1 ? NV_ENC_PIC_FLAG_FORCEIDR : (NV_ENC_PIC_FLAG_FORCEIDR | NV_ENC_PIC_FLAG_OUTPUT_SPSPPS);
#    else
        pic_params.encodePicFlags = NV_ENC_PIC_FLAG_FORCEIDR | NV_ENC_PIC_FLAG_OUTPUT_SPSPPS;
#    endif // USE_AV1
        pending_idrs_--;
    }

    check_nvenc(nvenc_.nvEncEncodePicture(encoder_, &pic_params), "Encode picture failed (encode_stereo)");
    check_nvenc(nvenc_.nvEncUnmapInputResource(encoder_, map_resource.mappedResource), "Unmap resource failed (encode_stereo)");

    // Lock bitstream and copy out.
    NV_ENC_LOCK_BITSTREAM lock_bitstream = {};
    lock_bitstream.version               = nvenc_struct_version(NV_ENC_LOCK_BITSTREAM_VER);
    lock_bitstream.outputBitstream       = output_buffer_;
    check_nvenc(nvenc_.nvEncLockBitstream(encoder_, &lock_bitstream), "Lock bitstream failed (encode_stereo)");

    std::vector<uint8_t> encoded_data;
    const auto*          bitstream_data = static_cast<const uint8_t*>(lock_bitstream.bitstreamBufferPtr);
    const size_t         bitstream_size = lock_bitstream.bitstreamSizeInBytes;

    const bool is_keyframe =
        (lock_bitstream.pictureType == NV_ENC_PIC_TYPE_IDR) || (lock_bitstream.pictureType == NV_ENC_PIC_TYPE_I);
    last_frame_was_keyframe_ = is_keyframe;

#    ifdef USE_AV1
    const bool prepend_headers = (codec_ != encoder_codec::av1) && is_keyframe && !vps_sps_pps_.empty();
#    else
    const bool prepend_headers = is_keyframe && !vps_sps_pps_.empty();
#    endif // USE_AV1

    if (prepend_headers) {
        encoded_data.reserve(vps_sps_pps_.size() + bitstream_size);
        encoded_data.insert(encoded_data.end(), vps_sps_pps_.begin(), vps_sps_pps_.end());
    }
    encoded_data.insert(encoded_data.end(), bitstream_data, bitstream_data + bitstream_size);

    check_nvenc(nvenc_.nvEncUnlockBitstream(encoder_, output_buffer_), "Unlock bitstream failed (encode_stereo)");

    return encoded_data;
}

std::vector<uint8_t> nvenc_encoder::encode_rgba_stereo(const uint8_t* left_rgba, size_t left_pitch,
                                                       const uint8_t* right_rgba, size_t right_pitch,
                                                       uint32_t source_width, uint32_t source_height, bool flip_y) {
    std::lock_guard<std::mutex> lock(encode_mutex_);
    if (!initialized_.load()) {
        throw std::runtime_error("Encoder not initialized");
    }
    check_cuda(cuCtxSetCurrent(cu_context_), "Set CUDA context failed");
    convert_rgba_stereo_to_nv12_gpu(left_rgba, left_pitch, right_rgba, right_pitch, source_width, source_height, flip_y);

    NV_ENC_MAP_INPUT_RESOURCE map_resource{};
    map_resource.version            = nvenc_struct_version(NV_ENC_MAP_INPUT_RESOURCE_VER);
    map_resource.registeredResource = registered_nv12_;
    check_nvenc(nvenc_.nvEncMapInputResource(encoder_, &map_resource),
                "Map input resource failed (encode_rgba_stereo)");

    NV_ENC_PIC_PARAMS pic_params{};
    pic_params.version         = nvenc_struct_version(NV_ENC_PIC_PARAMS_VER);
    pic_params.inputBuffer     = map_resource.mappedResource;
    pic_params.bufferFmt       = map_resource.mappedBufferFmt;
    pic_params.inputWidth      = aligned_width_;
    pic_params.inputHeight     = aligned_height_;
    pic_params.outputBitstream = output_buffer_;
    pic_params.pictureStruct   = NV_ENC_PIC_STRUCT_FRAME;
    pic_params.inputTimeStamp  = frame_count_++;
    if (pending_idrs_ > 0) {
#    ifdef USE_AV1
        const bool use_av1 = codec_ == encoder_codec::av1;
        pic_params.encodePicFlags =
            use_av1 ? NV_ENC_PIC_FLAG_FORCEIDR : (NV_ENC_PIC_FLAG_FORCEIDR | NV_ENC_PIC_FLAG_OUTPUT_SPSPPS);
#    else
        pic_params.encodePicFlags = NV_ENC_PIC_FLAG_FORCEIDR | NV_ENC_PIC_FLAG_OUTPUT_SPSPPS;
#    endif
        --pending_idrs_;
    }

    check_nvenc(nvenc_.nvEncEncodePicture(encoder_, &pic_params), "Encode picture failed (encode_rgba_stereo)");
    check_nvenc(nvenc_.nvEncUnmapInputResource(encoder_, map_resource.mappedResource),
                "Unmap resource failed (encode_rgba_stereo)");

    NV_ENC_LOCK_BITSTREAM bitstream{};
    bitstream.version         = nvenc_struct_version(NV_ENC_LOCK_BITSTREAM_VER);
    bitstream.outputBitstream = output_buffer_;
    check_nvenc(nvenc_.nvEncLockBitstream(encoder_, &bitstream), "Lock bitstream failed (encode_rgba_stereo)");

    const bool is_keyframe =
        bitstream.pictureType == NV_ENC_PIC_TYPE_IDR || bitstream.pictureType == NV_ENC_PIC_TYPE_I;
    last_frame_was_keyframe_ = is_keyframe;
#    ifdef USE_AV1
    const bool prepend_headers = codec_ != encoder_codec::av1 && is_keyframe && !vps_sps_pps_.empty();
#    else
    const bool prepend_headers = is_keyframe && !vps_sps_pps_.empty();
#    endif
    std::vector<uint8_t> encoded;
    if (prepend_headers) {
        encoded.insert(encoded.end(), vps_sps_pps_.begin(), vps_sps_pps_.end());
    }
    const auto* bytes = static_cast<const uint8_t*>(bitstream.bitstreamBufferPtr);
    encoded.insert(encoded.end(), bytes, bytes + bitstream.bitstreamSizeInBytes);
    check_nvenc(nvenc_.nvEncUnlockBitstream(encoder_, output_buffer_),
                "Unlock bitstream failed (encode_rgba_stereo)");
    return encoded;
}

#endif // COMBINED_ENCODING

void nvenc_encoder::request_idr() {
    std::lock_guard<std::mutex> lock(encode_mutex_);
    pending_idrs_ = std::max(pending_idrs_, 1);
}

// ============================================================================
// Error Checking
// ============================================================================

void nvenc_encoder::check_nvenc(NVENCSTATUS status, const char* msg) {
    if (status != NV_ENC_SUCCESS) {
        std::string error_msg = std::string(msg) + " Error: " + std::to_string(status);
        spdlog::get("illixr")->error("nvenc_encoder: {}", error_msg);
        throw std::runtime_error(error_msg);
    }
}

void nvenc_encoder::check_cuda(CUresult result, const char* msg) {
    if (result != CUDA_SUCCESS) {
        const char* error_name = nullptr;
        cuGetErrorName(result, &error_name);
        std::string error_msg = std::string(msg) + " Error: " + (error_name ? error_name : std::to_string(result));
        spdlog::get("illixr")->error("nvenc_encoder: {}", error_msg);
        throw std::runtime_error(error_msg);
    }
}

void nvenc_encoder::check_cuda_runtime(cudaError_t result, const char* msg) {
    if (result != cudaSuccess) {
        const char* e_msg_c = cudaGetErrorString(result);

        std::string e_msg;

        if (e_msg_c) {
            e_msg = std::string(e_msg_c);
        } else {
            e_msg = std::to_string(result);
        }

        std::string error_msg = std::string(msg) + " Error: " + e_msg;
        spdlog::get("illixr")->error("nvenc_encoder: {}", error_msg);
        throw std::runtime_error(error_msg);
    }
}

// ============================================================================
// Frame Saving (Debug)
// ============================================================================
#ifdef DUMP_FRAMES
void nvenc_encoder::init_frame_saver() {
    // Check environment variable to enable frame saving
    const char* env_enabled  = std::getenv("ILLIXR_SAVE_FRAMES");
    const char* env_interval = std::getenv("ILLIXR_SAVE_INTERVAL");

    frame_saver::config cfg;
    cfg.output_directory = "server_frames";
    cfg.prefix           = "server";
    cfg.save_ppm         = true;
    cfg.save_raw         = false;
    cfg.enabled          = (env_enabled != nullptr && std::string(env_enabled) == "1");

    if (env_interval) {
        try {
            cfg.save_interval = std::stoi(env_interval);
        } catch (...) {
            cfg.save_interval = 10;
        }
    } else {
        cfg.save_interval = 10;
    }

    if (cfg.enabled) {
        frame_saver_ = std::make_unique<frame_saver>(cfg);

        // Allocate host buffer for readback
        size_t buffer_size = width_ * height_ * 4; // BGRA
        cuda_host_buffer_.resize(buffer_size);

        spdlog::get("illixr")->info("nvenc_encoder: Frame saver enabled, "
                                    "encoder dims={}x{}, buffer={}bytes",
                                    width_, height_, buffer_size);
    } else {
        spdlog::get("illixr")->debug("nvenc_encoder: Frame saver disabled (set ILLIXR_SAVE_FRAMES=1 to enable)");
    }
}

void nvenc_encoder::save_cuda_frame_to_disk(const cuda_imported_vulkan_image& imported) {
    if (!frame_saver_)
        return;

    try {
        // Use the ACTUAL imported image dimensions, not the encoder dimensions
        // width_/height_ may differ from the imported image (e.g. per-eye vs full)
        uint32_t actual_width  = imported.width;
        uint32_t actual_height = imported.height;

        spdlog::get("illixr")->debug("nvenc_encoder: Saving frame - imported={}x{}, encoder={}x{}", actual_width, actual_height,
                                     width_, height_);

        // Resize host buffer if needed
        size_t required_size = actual_width * actual_height * 4;
        if (cuda_host_buffer_.size() < required_size) {
            cuda_host_buffer_.resize(required_size);
        }

#    if CUDA_VERSION >= 13000
        if (imported.rt_array == nullptr) {
            spdlog::get("illixr")->warn("nvenc_encoder: imported.rt_array is NULL, cannot save");
            return;
        }

        cudaError_t rt_err = cudaMemcpy2DFromArray(cuda_host_buffer_.data(),
                                                   actual_width * 4,        // dst pitch
                                                   imported.rt_array, 0, 0, // src x, y offset
                                                   actual_width * 4,        // width in bytes
                                                   actual_height, cudaMemcpyDeviceToHost);
        if (rt_err != cudaSuccess) {
            spdlog::get("illixr")->warn("nvenc_encoder: Failed to copy frame: {} (dims={}x{})", cudaGetErrorString(rt_err),
                                        actual_width, actual_height);
            return;
        }
#    else
        if (imported.array == nullptr) {
            spdlog::get("illixr")->warn("nvenc_encoder: imported.array is NULL, cannot save");
            return;
        }

        CUDA_MEMCPY2D copy_params = {};
        copy_params.srcMemoryType = CU_MEMORYTYPE_ARRAY;
        copy_params.srcArray      = imported.array;
        copy_params.srcXInBytes   = 0;
        copy_params.srcY          = 0;
        copy_params.dstMemoryType = CU_MEMORYTYPE_HOST;
        copy_params.dstHost       = cuda_host_buffer_.data();
        copy_params.dstPitch      = actual_width * 4; // BGRA/RGBA, 4 bytes per pixel
        copy_params.dstXInBytes   = 0;
        copy_params.dstY          = 0;
        copy_params.WidthInBytes  = actual_width * 4;
        copy_params.Height        = actual_height;

        CUresult result = cuMemcpy2D(&copy_params);
        if (result != CUDA_SUCCESS) {
            const char* error_name = nullptr;
            cuGetErrorName(result, &error_name);
            spdlog::get("illixr")->warn("nvenc_encoder: Failed to copy frame: {} (dims={}x{}, array={:p})",
                                        error_name ? error_name : "unknown", actual_width, actual_height,
                                        (void*) imported.array);
            return;
        }
#    endif // CUDA_VERSION >= 13000

        frame_saver_->save_bgra(cuda_host_buffer_.data(), actual_width, actual_height, 0, current_eye_index_, "color");

        spdlog::get("illixr")->debug("nvenc_encoder: Frame saved successfully {}x{}", actual_width, actual_height);

    } catch (const std::exception& e) {
        spdlog::get("illixr")->warn("nvenc_encoder: Frame save failed: {}", e.what());
    }
}
#endif
void nvenc_encoder::set_current_eye(int eye) {
    current_eye_index_ = eye;
}

void nvenc_encoder::send_startup_idrs(int count) {
    pending_idrs_ = count;
}
