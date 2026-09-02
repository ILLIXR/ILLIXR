#pragma once

#include "illixr/data_format/stereo_presentation.hpp"
#include "illixr/switchboard.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <Eigen/Core>
#include <Eigen/Geometry>
#include <string>

namespace ILLIXR::data_format {

/** Pixel storage understood by current Boba frame consumers. */
enum class stereo_pixel_format : std::uint8_t {
    rgba8_unorm,
};

/** Row-zero convention used when uploading a producer image to a GPU texture. */
enum class stereo_image_origin : std::uint8_t {
    upper_left,
    lower_left,
};

/** A byte range containing one tightly packed image in a shared-memory file. */
struct stereo_shared_image {
    /** Byte range and row layout inside `stereo_frame::pixel_buffer_path`. */
    std::uint64_t byte_offset{0};
    std::uint64_t byte_count{0};
    std::uint32_t width{0};
    std::uint32_t height{0};
    std::uint32_t row_stride_bytes{0};
};

/** Eye pose/FOV used when the producer rendered the corresponding image. */
struct stereo_render_view {
    /** Pose and asymmetric FOV used by Boba when this eye image was rendered. */
    Eigen::Vector3f    position{Eigen::Vector3f::Zero()};
    Eigen::Quaternionf orientation{Eigen::Quaternionf::Identity()};
    float              angle_left{0.0F};
    float              angle_right{0.0F};
    float              angle_up{0.0F};
    float              angle_down{0.0F};
    bool               valid{false};
};

/** Raw Boba viewer-overlay command. Its 14-float layout is preserved losslessly. */
struct stereo_overlay_command_range {
    /** Byte offset into `stereo_frame::overlay_buffer_path`. */
    std::uint64_t byte_offset{0};
    std::uint32_t command_count{0};
    std::uint32_t command_stride_floats{0};
};

/** Optional bitmap overlay; `source_row_stride_bytes` describes its source-ring layout. */
struct stereo_modal_overlay {
    /** Visibility and eye-specific projection validity for this frame. */
    bool visible{false};
    bool left_valid{false};
    bool right_valid{false};

    /** Bitmap range and source-row layout in `modal_buffer_path`. */
    std::uint64_t byte_offset{0};
    std::uint32_t width{0};
    std::uint32_t height{0};
    std::uint32_t source_row_stride_bytes{0};
    /** Per-eye projected corners in source-image pixel coordinates. */
    std::array<Eigen::Vector2f, 4> left_quad_pixels{};
    std::array<Eigen::Vector2f, 4> right_quad_pixels{};
    /** Physical dimensions supplied by Boba for native 3D presentation. */
    float width_m{0.0F};
    float height_m{0.0F};
};

/**
 * Stereo render result published on the `stereo_frame` switchboard topic.
 *
 * To avoid retaining hundreds of multi-megabyte image copies in switchboard's
 * history, the images are zero-copy references into a producer-owned mmap ring.
 * A consumer should map `pixel_buffer_path` once, verify that the uint64 value at
 * `pixel_generation_offset` equals `source_frame_id`, consume the image promptly,
 * and verify it again afterward. A mismatch means the ring slot was recycled and
 * the frame must be dropped. Overlay and modal rings use the same rule.
 */
struct stereo_frame : public switchboard::event {
    /** Producer sequence and capture time; source_frame_id is the ring generation. */
    std::uint64_t sequence{0};
    time_point    sample_time{};
    std::uint64_t source_frame_id{0};

    stereo_pixel_format      format{stereo_pixel_format::rgba8_unorm};
    stereo_image_origin      origin{stereo_image_origin::upper_left};
    stereo_presentation_mode presentation_mode{stereo_presentation_mode::stereo_fullscreen};

    /** Required pixel ring and generation marker for the selected slot. */
    std::string         pixel_buffer_path;
    std::uint64_t       pixel_generation_offset{0};
    stereo_shared_image left;
    stereo_shared_image right;
    stereo_render_view  left_render_view;
    stereo_render_view  right_render_view;

    /** Optional vector-overlay ring synchronized to `source_frame_id`. */
    std::string                  overlay_buffer_path;
    std::uint64_t                overlay_generation_offset{0};
    stereo_overlay_command_range left_overlay_commands;
    stereo_overlay_command_range right_overlay_commands;

    /** Optional modal-bitmap ring synchronized to `source_frame_id`. */
    std::string          modal_buffer_path;
    std::uint64_t        modal_generation_offset{0};
    stereo_modal_overlay modal;
};

} // namespace ILLIXR::data_format
