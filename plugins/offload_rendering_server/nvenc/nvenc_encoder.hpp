#pragma once

/// @file nvenc_encoder.hpp
/// @brief NVENC encoder (HEVC or AV1) with Vulkan-CUDA interop and GPU color conversion
///
/// This encoder accepts Vulkan images directly from the buffer pool
/// and encodes them using NVENC via CUDA-Vulkan interop.
/// Color conversion (BGRA→NV12) runs entirely on the GPU.
/// Works on both Windows and Linux without FFmpeg dependency.
///
/// Define USE_AV1 to encode with AV1 instead of HEVC.
/// AV1 requires an Ada Lovelace (RTX 40-series) or later GPU on the server and
/// Android 14+ (API 34) with hardware AV1 decode on the headset (Meta Quest 3).
/// AV1 typically produces sharper edges at the same bitrate owing to its more
/// expressive intra-prediction and in-loop filter set.
/// Without USE_AV1 the encoder defaults to HEVC (H.265).
#if defined(_WIN32) || defined(_WIN64)
    #ifndef VK_USE_PLATFORM_WIN32_KHR
        #define VK_USE_PLATFORM_WIN32_KHR
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#else
    #include <dlfcn.h>
    #include <unistd.h>
#endif
#ifdef DUMP_FRAMES
#include "frame_saver.hpp"
#endif

#include <atomic>
#include <cstdint>
#include <cuda.h>
#include <cuda_runtime.h>
#include <memory>
#include <mutex>
#include <nvEncodeAPI.h>
#include <vector>
#include <vulkan/vulkan.h>

// Load NVENC API functions
typedef NVENCSTATUS(NVENCAPI* PNVENCODEAPICREATEINSTANCE)(NV_ENCODE_API_FUNCTION_LIST*);

namespace ILLIXR {

// Forward declarations for CUDA kernel launchers
extern "C" {
cudaError_t launch_bgra_to_nv12(const uint8_t* src_bgra, uint8_t* dst_nv12, size_t src_pitch, size_t dst_pitch, uint32_t width,
                                uint32_t height, uint32_t aligned_height, cudaStream_t stream);

cudaError_t launch_bgra_texture_to_nv12(cudaTextureObject_t tex_obj, uint8_t* dst_nv12, size_t dst_pitch, uint32_t width,
                                        uint32_t height, uint32_t aligned_height, cudaStream_t stream);

/// Scaled variant: samples a normalized-coordinate texture and writes dst_width x dst_height
/// output pixels.  Works for any src:dst ratio (downscale, upscale, or 1:1).
/// The texture object MUST be created with normalizedCoords=1 and linear filtering.
cudaError_t launch_bgra_texture_to_nv12_scaled(cudaTextureObject_t tex_obj, uint8_t* dst_nv12, size_t dst_pitch,
                                               uint32_t dst_width, uint32_t dst_height, uint32_t aligned_height,
                                               cudaStream_t stream);

#ifdef COMBINED_ENCODING
/// Combined stereo blit: converts left and right eye textures side-by-side into a single NV12
/// buffer of total width (dst_eye_width * 2) in one CUDA kernel launch.  Both textures use
/// normalizedCoords=1 with bilinear filtering so any render-scale ratio is handled for free.
/// @param left_tex       Texture for the left  eye (normalizedCoords=1, linear filter).
/// @param right_tex      Texture for the right eye (normalizedCoords=1, linear filter).
/// @param dst_nv12       Output NV12 buffer (total row width = dst_eye_width * 2 pixels).
/// @param dst_pitch      Row pitch of dst_nv12 in bytes.
/// @param dst_eye_width  Per-eye output width in pixels (half of total combined width).
/// @param dst_height     Output height in pixels.
/// @param aligned_height NVENC-aligned height used to locate the UV plane.
cudaError_t launch_bgra_stereo_to_nv12(cudaTextureObject_t left_tex, cudaTextureObject_t right_tex,
                                        uint8_t* dst_nv12, size_t dst_pitch,
                                        uint32_t dst_eye_width, uint32_t dst_height,
                                        uint32_t aligned_height, cudaStream_t stream);
#endif // COMBINED_ENCODING

cudaError_t launch_rg_depth_to_nv12_scaled(cudaTextureObject_t tex_obj, uint8_t* dst_nv12, size_t dst_pitch, uint32_t dst_width,
                                           uint32_t dst_height, uint32_t aligned_height, cudaStream_t stream);

/// Motion-vector RGBA16F → NV12.
/// R=Vx → Y plane, G=Vy → UV.U, B=Vz → UV.V (Vy/Vz at half resolution).
/// Velocities are normalised from [-MV_MAX_VEL, +MV_MAX_VEL] to [0, 255].
/// The texture object MUST be created with normalizedCoords=1 and linear filtering.
cudaError_t launch_rgba16f_to_nv12_scaled(cudaTextureObject_t tex_obj, uint8_t* dst_nv12, size_t dst_pitch,
                                          uint32_t dst_width, uint32_t dst_height, uint32_t aligned_height,
                                          cudaStream_t stream);

/// Motion-vector RGBA16F → P010 (10-bit YUV 4:2:0, MSB-packed uint16_t).
/// R=Vx → Y plane, G=Vy → UV.U, B=Vz → UV.V (Vy/Vz at half resolution).
/// Velocities are normalised from [-MV_MAX_VEL, +MV_MAX_VEL] to [0, 1023].
/// Each sample is stored in the 10 MSBs of a uint16_t (P010 layout).
/// The texture object MUST be created with normalizedCoords=1 and linear
/// filtering, and with readMode=cudaReadModeElementType (float16 source).
cudaError_t launch_rgba16f_to_p010_scaled(cudaTextureObject_t tex_obj, uint16_t* dst_p010, size_t dst_pitch_bytes,
                                          uint32_t dst_width, uint32_t dst_height, uint32_t aligned_height,
                                          cudaStream_t stream);
}

/// Information about a Vulkan image to be encoded
struct vulkan_image_info {
    VkImage        image         = VK_NULL_HANDLE;
    VkDeviceMemory memory        = VK_NULL_HANDLE;
    VkDeviceSize   memory_size   = 0;
    VkDeviceSize   memory_offset = 0;
    uint32_t       width         = 0;
    uint32_t       height        = 0;
    VkFormat       format        = VK_FORMAT_UNDEFINED;
    VkImageTiling  tiling        = VK_IMAGE_TILING_OPTIMAL;
};

/// Vulkan device context for CUDA interop
struct vulkan_context {
    VkInstance       instance              = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device       = VK_NULL_HANDLE;
    VkDevice         device                = VK_NULL_HANDLE;
    VkQueue          graphics_queue        = VK_NULL_HANDLE;
    uint32_t         graphics_queue_family = 0;
    VkCommandPool    command_pool          = VK_NULL_HANDLE;

    // Extension function pointers
#ifdef _WIN32
    PFN_vkGetMemoryWin32HandleKHR    vkGetMemoryWin32HandleKHR    = nullptr;
    PFN_vkGetSemaphoreWin32HandleKHR vkGetSemaphoreWin32HandleKHR = nullptr;
#else
    PFN_vkGetMemoryFdKHR    vkGetMemoryFdKHR    = nullptr;
    PFN_vkGetSemaphoreFdKHR vkGetSemaphoreFdKHR = nullptr;
#endif
};

/// Imported Vulkan image for CUDA access
struct cuda_imported_vulkan_image {
    // -----------------------------------------------------------------------
    // CUDA Driver API members (used when CUDA_VERSION < 13000)
    // In CUDA 13, CUarray and cudaArray_t became incompatible opaque types,
    // so a direct reinterpret_cast is no longer valid.  The driver-API path
    // is therefore only compiled when targeting CUDA 12 or older.
    // -----------------------------------------------------------------------
#if CUDA_VERSION < 13000
    CUexternalMemory ext_memory = nullptr;
    CUmipmappedArray mipmap     = nullptr;
    CUarray          array      = nullptr;
#endif

    // -----------------------------------------------------------------------
    // CUDA Runtime API members (used when CUDA_VERSION >= 13000)
    // cudaExternalMemoryGetMappedMipmappedArray / cudaMipmappedArrayGetLevel
    // return cudaArray_t directly, avoiding any driver/runtime type mismatch.
    // -----------------------------------------------------------------------
#if CUDA_VERSION >= 13000
    cudaExternalMemory_t rt_ext_memory = nullptr;
    cudaMipmappedArray_t rt_mipmap     = nullptr;
    cudaArray_t          rt_array      = nullptr;
#endif

    // Texture object for efficient GPU read
    cudaTextureObject_t texture = 0;

    // Surface object for GPU write (if needed)
    cudaSurfaceObject_t surface = 0;

#ifdef _WIN32
    HANDLE handle = nullptr;
#else
    int fd = -1;
#endif

    uint32_t width      = 0;
    uint32_t height     = 0;
    bool     valid      = false;
    bool     is_float16 = false; ///< true when the source Vulkan format is a 16-bit float format (e.g. RGBA16F)
};

/// Selects the pixel-format path used during GPU colour conversion.
enum class encoder_mode {
    color,         ///< BGRA → NV12 via BT.709 (default)
    depth,         ///< RG → NV12 (16-bit depth, two bytes preserved)
    motion_vector, ///< RGBA16F → NV12 (Vx→Y, Vy→U, Vz→V, normalised)
};

/// Selects the video codec used for encoding.
/// Controlled by the USE_AV1 preprocessor directive: when defined the default
/// becomes av1; otherwise it remains hevc.  The value can also be passed
/// explicitly to the constructor if mixed-codec sessions are ever needed.
enum class encoder_codec {
#ifdef USE_AV1
    av1,           ///< AV1 (requires Ada Lovelace / RTX 40-series or later)
    hevc,
    default_codec = av1,
#else
    hevc,          ///< HEVC / H.265 (default)
    av1,
    default_codec = hevc,
#endif
};

/// NVENC HEVC encoder with direct Vulkan image input and GPU color conversion
class nvenc_encoder {
public:
    /// Constructor
    /// @param width     Image width
    /// @param height    Image height
    /// @param bitrate   Target bitrate in bps (default 15 Mbps)
    /// @param framerate Target framerate (default 72 fps)
    /// @param mode      Pixel-format conversion path (default: color)
    /// @param codec     Video codec to use (default: controlled by USE_AV1 define)
    nvenc_encoder(uint32_t width, uint32_t height, int64_t bitrate = 15000000, int framerate = 72,
                  encoder_mode mode = encoder_mode::color,
                  encoder_codec codec = encoder_codec::default_codec);

    /// Destructor - releases all resources
    ~nvenc_encoder();

    /// Initialize the encoder with Vulkan context
    /// @param vk_ctx Vulkan context from display provider
    /// @return true if initialization succeeded
    bool initialize(const vulkan_context& vk_ctx);

    /// Import a Vulkan image for encoding
    /// @param vk_image Vulkan image info
    /// @return Index of imported image, or -1 on failure
    int import_vulkan_image(const vulkan_image_info& vk_image);

    /// Encode a previously imported Vulkan image (GPU color conversion)
    /// @param imported_index Index returned from import_vulkan_image
    /// @return Encoded bitstream data (HEVC)
    std::vector<uint8_t> encode(int imported_index);

#ifdef COMBINED_ENCODING
    /// Encode two previously imported Vulkan images (left and right eye) side-by-side into a
    /// single HEVC bitstream using one CUDA kernel launch and one NVENC call.  The encoder must
    /// have been constructed with width = (per_eye_width * 2) so that its NV12 buffer and NVENC
    /// session cover the full combined frame.
    /// @param left_index   Index returned from import_vulkan_image for the left  eye.
    /// @param right_index  Index returned from import_vulkan_image for the right eye.
    /// @return Encoded bitstream data (HEVC) for the combined stereo frame.
    std::vector<uint8_t> encode_stereo(int left_index, int right_index);
#endif // COMBINED_ENCODING

    /// Encode a Vulkan image directly (imports temporarily)
    /// @param vk_image Vulkan image info
    /// @return Encoded bitstream data (HEVC)
    std::vector<uint8_t> encode_image(const vulkan_image_info& vk_image);

    /// Check if encoder is initialized
    [[nodiscard]] bool is_initialized() const {
        return initialized_.load();
    }

    /// Returns true if the most recently encoded frame was a keyframe (IDR or I-frame).
    /// Use this to set the is_keyframe flag when calling queue_encoded_data on the
    /// receiver, instead of attempting to parse OBU/NAL headers on the sending side.
    [[nodiscard]] bool last_frame_was_keyframe() const {
        return last_frame_was_keyframe_;
    }

    /// Get aligned width
    [[nodiscard]] uint32_t get_aligned_width() const {
        return aligned_width_;
    }

    /// Get aligned height
    [[nodiscard]] uint32_t get_aligned_height() const {
        return aligned_height_;
    }

    /// Get VPS/SPS/PPS headers
    [[nodiscard]] const std::vector<uint8_t>& get_headers() const {
        return vps_sps_pps_;
    }

    /// Set the current eye index for frame saving
    /// @param eye Eye index (0=left, 1=right)
    void set_current_eye(int eye);

private:
    void init_cuda();
    void init_nvenc();
    void query_capabilities();
    void init_encoder();
    void create_buffers();
    void get_sequence_headers();
    void send_startup_idrs(int count);

    #ifdef DUMP_FRAMES
    // Frame saving (debug)
    void init_frame_saver();
    void save_cuda_frame_to_disk(const cuda_imported_vulkan_image& imported);
    #endif

    // CUDA-Vulkan interop
    bool import_vulkan_memory(const vulkan_image_info& vk_image, cuda_imported_vulkan_image& imported);
    bool create_texture_object(cuda_imported_vulkan_image& imported);
    void release_imported_image(cuda_imported_vulkan_image& imported);

    // GPU color conversion
    void convert_bgra_to_nv12_gpu(const cuda_imported_vulkan_image& imported);

#ifdef COMBINED_ENCODING
    /// GPU stereo blit: converts left and right eye textures into the combined NV12 buffer in
    /// one CUDA kernel launch.  width_ must equal the per-eye target width (half of the total).
    void convert_stereo_to_nv12_gpu(const cuda_imported_vulkan_image& left,
                                     const cuda_imported_vulkan_image& right);
#endif // COMBINED_ENCODING

    // Error checking

    static void check_nvenc(NVENCSTATUS status, const char* msg);
    static void check_cuda(CUresult result, const char* msg);
    static void check_cuda_runtime(cudaError_t result, const char* msg);

    // Vulkan context
    vulkan_context vk_ctx_;

    // CUDA context
    CUcontext    cu_context_ = nullptr;
    CUdevice     cu_device_  = 0;
    cudaStream_t cu_stream_  = nullptr;

    // CUDA buffers for encoding
    CUdeviceptr cuda_nv12_buffer_ = 0;
    size_t      cuda_nv12_pitch_  = 0;

    // NVENC state
    void*                       encoder_ = nullptr;
    NV_ENCODE_API_FUNCTION_LIST nvenc_{};
    NV_ENC_REGISTERED_PTR       registered_nv12_ = nullptr;
    NV_ENC_OUTPUT_PTR           output_buffer_   = nullptr;

    // Encoder parameters
    uint32_t width_;
    uint32_t height_;
    uint32_t aligned_width_;
    uint32_t aligned_height_;
    int64_t  bitrate_;
    int      framerate_;

    // State
    std::atomic<bool>    initialized_{false};
    bool                 needs_downscale_      = false; ///< true when source images are larger than encode dims
    bool                 last_frame_was_keyframe_ = false; ///< set after each encode() / encode_stereo() call
    std::vector<uint8_t> vps_sps_pps_;
    uint64_t             frame_count_ = 0;

    encoder_mode mode_ = encoder_mode::color; ///< conversion path selected at construction
    encoder_codec codec_ = encoder_codec::default_codec; ///< video codec selected at construction

    // Imported images cache
    std::vector<cuda_imported_vulkan_image> imported_images_;

    // Thread safety
    std::mutex encode_mutex_;

    #ifdef DUMP_FRAMES
    // Frame saving (debug) - enabled via ILLIXR_SAVE_FRAMES=1 environment variable
    std::unique_ptr<frame_saver> frame_saver_;
    #endif
    std::vector<uint8_t> cuda_host_buffer_; // Host buffer for GPU readback
    int current_eye_index_ = 0;
    int pending_idrs_      = 0;
};
} // namespace ILLIXR