#pragma once

#ifdef Success
#    undef Success // For 'Success' conflict
#endif

#include "illixr/data_format/poses/head_pose.hpp"
#include "illixr/data_format/stereo_presentation.hpp"
#include "illixr/switchboard.hpp"

#include <array>
#include <boost/serialization/export.hpp>
#include <cstdint>
#include <memory>
#include <vector>
#ifdef __ANDROID__
#    include <GLES/gl.h>
#    ifndef NVDEC_DECODER
#        define NVDEC_DECODER
#    endif
#    ifdef ILLIXR_VULKAN
#        include <android/hardware_buffer.h>
#    endif // ILLIXR_VULKAN
#else
#    include <GL/gl.h>
#endif // __ANDROID__

// Define PACKET_TYPE based on available codec backends
#ifdef ILLIXR_LIBAV
extern "C" {
#    include "libavcodec_illixr/avcodec.h"
#    include "libavformat_illixr/avformat.h"
#    include "libavutil_illixr/hwcontext.h"
#    include "libavutil_illixr/opt.h"
#    include "libavutil_illixr/pixdesc.h"
}
#    define PACKET_TYPE AVPacket*
#    define PACKET_REF  AVPacket*
#elif defined(NVENC_ENCODER) || defined(NVDEC_DECODER)

#    define PACKET_TYPE std::vector<uint8_t>
#    define PACKET_REF  std::vector<uint8_t>&

#endif

#ifndef BUFFER_TYPE
#    ifdef USING_OPENXR
#        ifdef ENABLE_MONADO
#            define BUFFER_TYPE std::array<xrt_pose, 2>
#        else
#            define BUFFER_TYPE std::array<XrPosef, 2>
#        endif
#    else
#        ifndef BUFFER_TYPE
#            define BUFFER_TYPE data_format::pose::fast_head_pose_type
#        endif
#    endif
#endif

namespace ILLIXR::data_format {

/**
 * Boba's lightweight per-eye viewer overlays. Each command retains the
 * producer's 14-float layout and is interpreted in source-image pixels.
 * Keeping these commands out of the video bitstream lets the Quest render
 * controller rays and placement markers at the OpenXR swapchain resolution.
 */
struct boba_frame_overlay {
    static constexpr std::uint32_t command_stride_floats = 14;
    static constexpr std::uint32_t max_commands_per_eye  = 256;

    std::uint32_t      source_width{0};
    std::uint32_t      source_height{0};
    std::vector<float> left_commands;
    std::vector<float> right_commands;
};

/** Per-frame placement metadata for Boba's optional bitmap UI card. */
struct boba_modal_overlay {
    bool visible{false};
    bool left_valid{false};
    bool right_valid{false};

    std::uint64_t       texture_id{0};
    std::uint32_t       width{0};
    std::uint32_t       height{0};
    std::array<float, 8> left_quad_pixels{};
    std::array<float, 8> right_quad_pixels{};
    float                width_m{0.0F};
    float                height_m{0.0F};
};

/**
 * Reliable, content-addressed update for a Boba modal texture. The texture is
 * sent only when it changes (or periodically for recovery) and cached by the
 * Quest; boba_modal_overlay carries its per-frame projected placement.
 */
struct boba_modal_texture : public switchboard::event {
    static constexpr std::uint64_t wire_magic = 0x424F42414D4F444CULL; // "BOBAMODL"

    std::uint64_t             texture_id{0};
    std::uint32_t             width{0};
    std::uint32_t             height{0};
    std::vector<std::uint8_t> rgba;
    std::uint64_t             magic{wire_magic};
};

// Using arrays as a swapchain
// Array of left eyes, array of right eyes
// This more closely matches the format used by Monado
struct [[maybe_unused]] rendered_frame : public switchboard::event {
    std::array<GLuint, 2>                  swapchain_indices{}; // Index of image rendered for left and right swapchain
    [[maybe_unused]] std::array<GLuint, 2> swap_indices{};      // Which element of the swapchain
    pose::fast_head_pose_type              render_pose;         // The pose used when rendering this frame.
    time_point                             sample_time{};
    time_point                             render_time{};

    rendered_frame() = default;

    rendered_frame(std::array<GLuint, 2>&& swapchain_indices_, std::array<GLuint, 2>&& swap_indices_,
                   pose::fast_head_pose_type render_pose_, time_point sample_time_, time_point render_time_)
        : swapchain_indices{swapchain_indices_}
        , swap_indices{swap_indices_}
        , render_pose(std::move(render_pose_))
        , sample_time(sample_time_)
        , render_time(render_time_) { }
};

struct compressed_frame : public switchboard::event {
    bool    nalu_only{false};
    char*   left_color_nalu{nullptr};
    char*   right_color_nalu{nullptr};
    char*   left_depth_nalu{nullptr};
    char*   right_depth_nalu{nullptr};
    int32_t left_color_nalu_size{0};
    int32_t right_color_nalu_size{0};
    int32_t left_depth_nalu_size{0};
    int32_t right_depth_nalu_size{0};

    bool use_depth{false};
    bool use_motion_vectors{false};
    stereo_presentation_mode presentation_mode{stereo_presentation_mode::stereo_fullscreen};
    float                    content_aspect_ratio{0.0F};
    boba_frame_overlay       boba_overlay{};
    boba_modal_overlay       boba_modal{};

    /// Tag type: disambiguates the color+motion_vec constructor from color+depth.
    struct [[maybe_unused]] has_motion_vectors_tag { };
#if defined(ILLIXR_LIBAV) || defined(NVENC_ENCODER) || defined(NVDEC_DECODER)
    PACKET_TYPE left_color;
    PACKET_TYPE right_color;

    PACKET_TYPE left_depth;
    PACKET_TYPE right_depth;

    PACKET_TYPE left_motion_vec;
    PACKET_TYPE right_motion_vec;

#endif

    BUFFER_TYPE pose{};

    // Projection clip planes forwarded from XrCompositionLayerDepthInfoKHR.
    // The decoder uses these to linearise the encoded RG depth values back into
    // view-space metres: depth_m = near_z * far_z / (far_z - ndc * (far_z - near_z))
    // where ndc is the reconstructed [0,1] normalised depth.
    // Both are 0 when no depth layer was submitted this frame.
    float near_z = 0.0f;
    float far_z  = 0.0f;

    // Render FOV angles (radians) used by the server when encoding this frame.
    // Forwarded to the headset so xrEndFrame can use the correct render FOV
    // for timewarp, enabling overdraw margins to be used correctly.
#ifdef USING_OPENXR
    std::array<float, 2> fov_left  = {0.0f, 0.0f};
    std::array<float, 2> fov_right = {0.0f, 0.0f};
    std::array<float, 2> fov_up    = {0.0f, 0.0f};
    std::array<float, 2> fov_down  = {0.0f, 0.0f};
#endif

    uint64_t sent_time{0};
    uint64_t frame_number{0};
    uint64_t pose_id{0}; ///< ID of the combined_pose used to generate this frame,
                         ///< used to correlate rendered frames with headset pose measurements
    double encode_time = 0.;
    // True if the color bitstream for this frame is a keyframe (IDR / AV1 KEY_FRAME).
    // Set by the server from nvenc_encoder::last_frame_was_keyframe() immediately
    // after encoding, so the client never needs to parse OBU or NAL headers.
    bool    is_keyframe = false;
    int64_t magic       = 0; // Changed from long to int64_t for cross-platform compatibility

    compressed_frame() = default;

#if defined(ILLIXR_LIBAV) || defined(NVENC_ENCODER) || defined(NVDEC_DECODER)
    /// Color-only constructor.
    compressed_frame(PACKET_REF left_color, PACKET_REF right_color, const BUFFER_TYPE& pose, uint64_t sent_time,
                     uint64_t frame_no, bool nalu_only = false)
        : nalu_only(nalu_only)
        , use_depth(false)
        , use_motion_vectors(false)
        , left_color(left_color)
        , right_color(right_color)
        , left_depth{}
        , right_depth{}
        , left_motion_vec{}
        , right_motion_vec{}
        , pose(pose)
        , near_z(0.0f)
        , far_z(0.0f)
        , sent_time(sent_time)
        , frame_number(frame_no)
        , pose_id(0)
        , magic(0xdeadbeef) { }

    /// Color + depth constructor.
    compressed_frame(PACKET_REF left_color, PACKET_REF right_color, PACKET_REF left_depth, PACKET_REF right_depth,
                     const BUFFER_TYPE pose, uint64_t sent_time, uint64_t frame_no, float near_z, float far_z,
                     bool nalu_only = false)
        : nalu_only(nalu_only)
        , use_depth(true)
        , use_motion_vectors(false)
        , left_color(left_color)
        , right_color(right_color)
        , left_depth(left_depth)
        , right_depth(right_depth)
        , left_motion_vec{}
        , right_motion_vec{}
        , pose(pose)
        , near_z(near_z)
        , far_z(far_z)
        , sent_time(sent_time)
        , frame_number(frame_no)
        , pose_id(0)
        , magic(0xdeadbeef) { }

    /// Color + depth + motion vectors constructor.
    compressed_frame(PACKET_REF left_color, PACKET_REF right_color, PACKET_REF left_depth, PACKET_REF right_depth,
                     PACKET_REF left_motion_vec, PACKET_REF right_motion_vec, const BUFFER_TYPE pose, uint64_t sent_time,
                     uint64_t frame_no, float near_z, float far_z, bool nalu_only = false)
        : nalu_only(nalu_only)
        , use_depth(true)
        , use_motion_vectors(true)
        , left_color(left_color)
        , right_color(right_color)
        , left_depth(left_depth)
        , right_depth(right_depth)
        , left_motion_vec(left_motion_vec)
        , right_motion_vec(right_motion_vec)
        , pose(pose)
        , near_z(near_z)
        , far_z(far_z)
        , sent_time(sent_time)
        , frame_number(frame_no)
        , pose_id(0)
        , magic(0xdeadbeef) { }

#endif
    ~compressed_frame() override {
        if (nalu_only && left_color_nalu != nullptr && right_color_nalu != nullptr) {
            free(left_color_nalu);
            free(right_color_nalu);
            if (use_depth) {
                free(left_depth_nalu);
                free(right_depth_nalu);
            }
        }
    }
};

/// Decoded video frame data format
enum class frame_format : uint8_t {
    nv12,                   ///< NV12 (Y plane + interleaved UV plane)
    rgba8 [[maybe_unused]], ///< RGBA 8-bit per channel
    external_oes,           ///< GL_TEXTURE_EXTERNAL_OES handle (for zero-copy path)
#ifdef __ANDROID__
    hardware_buffer ///< AHardwareBuffer handle (Vulkan zero-copy path, Android only)
#endif
};

/// Single eye frame data.
/// On Android, the Vulkan path uses hw_buffer exclusively; the GL paths use
/// texture_id / data.  Only one set of fields is populated at a time.
struct eye_frame {
    /// Raw pixel data (used when format is nv12 or rgba8)
    std::vector<uint8_t> data{};

    /// GL texture ID (used when format is external_oes)
    /// This texture is owned by the decoder and valid until the next frame
    GLuint texture_id{0};

    /// Texture transform matrix from SurfaceTexture (4x4, column-major)
    /// Only valid when format is external_oes
    std::array<float, 16> texture_transform{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                                            0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
#if defined(__ANDROID__) && defined(ILLIXR_VULKAN)
    /// Hardware buffer handle (used when format is hardware_buffer).
    /// Owned by the AImageReader inside frame_decoder; valid until the next
    /// call to frame_decoder::release_image().  The renderer must not retain
    /// this pointer across frames.
    AHardwareBuffer* hw_buffer{nullptr};
#endif

    eye_frame() = default;

    /// Construct with raw data
    explicit eye_frame(std::vector<uint8_t> raw_data)
        : data{std::move(raw_data)}
        , texture_id{0} { }

    /// Construct with texture handle and transform
    eye_frame(GLuint tex_id, const float* transform)
        : texture_id{tex_id} {
        if (transform) {
            std::copy(transform, transform + 16, texture_transform.begin());
        }
    }
#if defined(__ANDROID__) && defined(ILLIXR_VULKAN)
    /// Construct with hardware buffer (Vulkan zero-copy path)
    explicit eye_frame(AHardwareBuffer* buf)
        : texture_id{0}
        , hw_buffer{buf} { }
#endif

    /// Check if this frame has valid data
    [[nodiscard]] [[maybe_unused]] bool has_data() const {
#if defined(__ANDROID__) && defined(ILLIXR_VULKAN)
        return !data.empty() || texture_id != 0 || hw_buffer != nullptr;
#else
        return !data.empty() || texture_id != 0;
#endif
    }

    /// Get Y plane pointer for NV12 format
    [[nodiscard]] [[maybe_unused]] const uint8_t* get_y_plane() const {
        return data.data();
    }

    /// Get UV plane pointer for NV12 format
    [[nodiscard]] [[maybe_unused]] const uint8_t* get_uv_plane(int width, int height) const {
        return data.data() + (width * height);
    }
};

/// Dual-eye video frame for stereo VR rendering.
/// Published by the decoder plugin, consumed by the renderer and oxr_interface.
///
/// Field ownership (hardware_buffer format):
///   left_eye / right_eye          — color AHardwareBuffers, retained by this struct.
///   left_depth / right_depth      — depth AHardwareBuffers, retained by this struct.
///   left_motion_vec / right_mv    — motion-vector AHardwareBuffers, retained by this struct.
///
/// The switchboard consumer (oxr_interface) must call
/// stereo_surface_decoder::release_frame() after the Vulkan submission that
/// reads these buffers has completed.
struct [[maybe_unused]] dual_frames : public switchboard::event {
    /// Color eye images
    eye_frame left_eye{};
    eye_frame right_eye{};

    /// Depth eye images (432×432, valid when has_valid_depth() is true)
    eye_frame left_depth{};
    eye_frame right_depth{};

#if defined(__ANDROID__) && defined(ILLIXR_VULKAN)
    /// Motion-vector eye images (432×432 HEVC 10-bit decoded,
    /// valid when has_valid_motion_vectors() is true)
    eye_frame left_motion_vec{};
    eye_frame right_motion_vec{};
#endif // defined(__ANDROID__) && defined(ILLIXR_VULKAN)

    int width{0};
    int height{0};

    // Format of the frame data
    frame_format format{frame_format::nv12};

    // Presentation timestamp
    time_point render_time{};

    BUFFER_TYPE pose{};

    // Frame sequence number (for debugging/sync)
    uint64_t frame_number{0};

    // ID of the combined_pose that was used to render this frame on the server.
    // Used by oxr_interface to look up the original headset pose measurement
    // for latency and accuracy logging.
    uint64_t pose_id{0};
    double   encode_time{0.};
    stereo_presentation_mode presentation_mode{stereo_presentation_mode::stereo_fullscreen};
    float                    content_aspect_ratio{0.0F};
    boba_frame_overlay       boba_overlay{};
    boba_modal_overlay       boba_modal{};
    std::shared_ptr<const std::vector<std::uint8_t>> boba_modal_rgba{};
    // Projection clip planes forwarded from the server's compressed_frame.
    // Required by XrCompositionLayerDepthInfoKHR and
    // XrCompositionLayerSpaceWarpInfoFB (nearZ / farZ fields).
    // Zero-initialised; only meaningful when has_valid_depth() is true.
    float near_z{0.0f};
    float far_z{0.0f};

#ifdef USING_OPENXR
    /// Render FOV angles forwarded from compressed_frame.
    /// Used by oxr_interface in XrCompositionLayerProjectionView::fov.
    std::array<float, 2> fov_left  = {0.0f, 0.0f};
    std::array<float, 2> fov_right = {0.0f, 0.0f};
    std::array<float, 2> fov_up    = {0.0f, 0.0f};
    std::array<float, 2> fov_down  = {0.0f, 0.0f};
#endif

    bool has_depth{false};
#if defined(__ANDROID__) && defined(ILLIXR_VULKAN)
    bool has_motion_vectors{false};
#endif // defined(__ANDROID__) && defined(ILLIXR_VULKAN)
    dual_frames() = default;

    /// Construct with raw NV12 data
    dual_frames(time_point tp, std::vector<uint8_t> left, std::vector<uint8_t> right, int w, int h, uint64_t frame_num = 0,
                bool has_depth = false)
        : left_eye{std::move(left)}
        , right_eye{std::move(right)}
        , width{w}
        , height{h}
        , format{frame_format::nv12}
        , render_time{tp}
        , frame_number{frame_num}
        , has_depth{has_depth} { }

#ifndef ILLIXR_VULKAN
    /// Construct with external OES texture handles (GL path)
    dual_frames(time_point tp, GLuint left_tex, const float* left_transform, GLuint right_tex, const float* right_transform,
                int w, int h, uint64_t frame_num = 0, bool has_depth = false)
        : left_eye{left_tex, left_transform}
        , right_eye{right_tex, right_transform}
        , width{w}
        , height{h}
        , format{frame_format::external_oes}
        , render_time{tp}
        , frame_number{frame_num}
        , has_depth{has_depth} { }

    /// Construct with external OES texture handles for BOTH color and depth (GL path)
    dual_frames(time_point tp, GLuint left_tex, const float* left_transform, GLuint right_tex, const float* right_transform,
                GLuint left_depth_tex, const float* left_depth_transform, GLuint right_depth_tex,
                const float* right_depth_transform, int w, int h, uint64_t frame_num = 0)
        : left_eye{left_tex, left_transform}
        , right_eye{right_tex, right_transform}
        , left_depth{left_depth_tex, left_depth_transform}
        , right_depth{right_depth_tex, right_depth_transform}
        , width{w}
        , height{h}
        , format{frame_format::external_oes}
        , render_time{tp}
        , frame_number{frame_num}
        , has_depth{true} { }
#endif

#if defined(__ANDROID__) && defined(ILLIXR_VULKAN)
    /// Construct with AHardwareBuffer handles for color only (Vulkan zero-copy path)
    dual_frames(time_point tp, AHardwareBuffer* left_buf, AHardwareBuffer* right_buf, int w, int h, uint64_t frame_num = 0)
        : left_eye{left_buf}
        , right_eye{right_buf}
        , width{w}
        , height{h}
        , format{frame_format::hardware_buffer}
        , render_time{tp}
        , frame_number{frame_num}
        , has_depth{false}
        , has_motion_vectors{false} { }

    /// Construct with AHardwareBuffer handles for color AND depth (Vulkan zero-copy path)
    dual_frames(time_point tp, AHardwareBuffer* left_color_buf, AHardwareBuffer* right_color_buf,
                AHardwareBuffer* left_depth_buf, AHardwareBuffer* right_depth_buf, int w, int h, uint64_t frame_num = 0)
        : left_eye{left_color_buf}
        , right_eye{right_color_buf}
        , left_depth{left_depth_buf}
        , right_depth{right_depth_buf}
        , width{w}
        , height{h}
        , format{frame_format::hardware_buffer}
        , render_time{tp}
        , frame_number{frame_num}
        , has_depth{true}
        , has_motion_vectors{false} { }

    /// Construct with AHardwareBuffer handles for color, depth, and motion vector (Vulkan zero-copy path)
    dual_frames(time_point tp, AHardwareBuffer* left_color_buf, AHardwareBuffer* right_color_buf,
                AHardwareBuffer* left_depth_buf, AHardwareBuffer* right_depth_buf, AHardwareBuffer* left_motion_buf,
                AHardwareBuffer* right_motion_buf, int w, int h, uint64_t frame_num = 0)
        : left_eye{left_color_buf}
        , right_eye{right_color_buf}
        , left_depth{left_depth_buf}
        , right_depth{right_depth_buf}
        , left_motion_vec{left_motion_buf}
        , right_motion_vec{right_motion_buf}
        , width{w}
        , height{h}
        , format{frame_format::hardware_buffer}
        , render_time{tp}
        , frame_number{frame_num}
        , has_depth{true}
        , has_motion_vectors{true} { }
#endif // defined(__ANDROID__) && defined(ILLIXR_VULKAN)

    /// Check if both eyes have valid data
    [[nodiscard]] bool is_valid() const {
        if (format == frame_format::external_oes) {
            return left_eye.texture_id != 0 && right_eye.texture_id != 0;
        }
#if defined(__ANDROID__) && defined(ILLIXR_VULKAN)
        if (format == frame_format::hardware_buffer) {
            return left_eye.hw_buffer != nullptr && right_eye.hw_buffer != nullptr;
        }
#endif
        return !left_eye.data.empty() && !right_eye.data.empty();
    }

    [[nodiscard]] bool has_valid_depth() const {
        if (!has_depth)
            return false;
#if defined(__ANDROID__) && defined(ILLIXR_VULKAN)
        if (format == frame_format::hardware_buffer) {
            return left_depth.hw_buffer != nullptr && right_depth.hw_buffer != nullptr;
        }
#endif
        return left_depth.texture_id != 0 && right_depth.texture_id != 0;
    }

#if defined(__ANDROID__) && defined(ILLIXR_VULKAN)
    /// Check if both motion-vector eye images are populated and valid
    [[nodiscard]] bool has_valid_motion_vectors() const {
        return has_motion_vectors && left_motion_vec.hw_buffer != nullptr && right_motion_vec.hw_buffer != nullptr;
    }
#endif // defined(__ANDROID__) && defined(ILLIXR_VULKAN)
};
} // namespace ILLIXR::data_format
