#pragma once

#include "illixr/switchboard.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace ILLIXR::data_format {

/**
 * @brief Pinhole camera intrinsics scaled to the delivered image resolution.
 *
 * For the RGB camera these are read from Camera2 ACameraMetadata
 * (ACAMERA_LENS_INTRINSIC_CALIBRATION) at the delivered resolution.
 *
 * For the depth sensor these are derived from XrDepthSensorIntrinsicsMETA
 * FOV tangents using the standard pinhole model:
 *   fx = width  / (tan_right + tan_left)
 *   fy = height / (tan_top   + tan_down)
 *   cx = tan_left * fx
 *   cy = tan_top  * fy   (cy from top — image is delivered top-down)
 */
struct camera_intrinsics {
    float   fx     = 0.f; //!< Horizontal focal length in pixels
    float   fy     = 0.f; //!< Vertical focal length in pixels
    float   cx     = 0.f; //!< Principal point x in pixels
    float   cy     = 0.f; //!< Principal point y in pixels
    int32_t width  = 0;
    int32_t height = 0;
};

/**
 * @brief A single co-captured, HEVC-encoded RGB frame and raw depth frame,
 *        each with a matched head pose at their individual sensor exposure time.
 *
 * Produced by xr_sensor_capture and consumed by the network transmission
 * plugin. Replaces the earlier semantic_data struct.
 *
 * RGB data
 * --------
 * H.265 annexb, encoded on the GPU by Android MediaCodec in Surface-input
 * mode. Camera2 writes directly into the encoder's ANativeWindow; the
 * Snapdragon hardware handles NV12 → HEVC without any CPU copy.
 * The timestamp is recovered from AMediaCodecBufferInfo.presentationTimeUs
 * (CLOCK_BOOTTIME microseconds, promoted to nanoseconds).
 *
 * Depth data
 * ----------
 * R16_UNORM (uint16), 2 bytes/pixel, top-down, unencoded.
 * Values are inverted NDC normalized uint16:
 *   depth_m = depth_near_z / (uint16_value / 65535.0)
 * XR_META_environment_depth on Quest 3 with Vulkan delivers VK_FORMAT_R16_UNORM.
 * CPU-side as delivered by XR_META_depth_sensor.
 *
 * Poses
 * -----
 * Row-major 4×4 float matrices in OpenXR right-handed tracking space.
 * Coordinate conversion to Unity left-handed world space is the server's
 * responsibility, consistent with the existing LhToRh convention.
 */
struct semantic_frame : switchboard::event {
    // ----- RGB -----
    std::vector<uint8_t> image;      //!< H.265 annexb bytes
    camera_intrinsics    intrinsics; //!< RGB camera intrinsics (includes width/height)

    // ----- Depth -----
    std::vector<uint8_t> depth; //!< R16_UNORM uint16, inverted NDC, top-down
    float                depth_near_z{0.f};
    camera_intrinsics    depth_intrinsics;

    // ----- Poses (row-major 4x4 matrices) -----
    float                rgb_camera_pose[16]{0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f};
    float                depth_pose[16]{0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f};

    // ----- Metadata -----
    float                max_depth{0.f};
    int32_t              frame_number{0};

    // ----- Timestamps -----
    int64_t  rgb_timestamp_ns   = 0;      //!< CLOCK_BOOTTIME ns (from encoder PTS)
    int64_t  depth_timestamp_ns = 0;      //!< XrTime ns (from XrDepthSensorDataMETA)

    semantic_frame() = default;
};


} // namespace ILLIXR::data_format
