#pragma once

#include "illixr/data_format/semantics.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/plugin.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"

#include <camera/NdkCameraCaptureSession.h>
#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraManager.h>
#include <camera/NdkCameraMetadata.h>
#include <cstdint>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <vector>

namespace ILLIXR {

/**
 * @brief ILLIXR plugin that co-captures HEVC-encoded RGB frames, depth frames,
 *        and per-sensor matched head poses on the Meta Quest 3.
 *
 * Acquisition and encoding pipeline
 * ----------------------------------
 * RGB:   Camera2 writes directly into MediaCodec's ANativeWindow input surface.
 *        The Snapdragon hardware handles NV12 → HEVC without any CPU copy.
 *        The encoder is initialised first so its ANativeWindow can be used as
 *        the Camera2 capture target. Completed H.265 output buffers are drained
 *        on each threadloop tick.
 *        RGB pose: xrLocateSpace() at the encoder output presentationTimeUs.
 *
 * Depth: XR_META_environment_depth OpenXR extension (standard Khronos openxr.h,
 *        no Meta SDK headers needed). Depth is delivered as an OpenGL ES texture
 *        via XrEnvironmentDepthSwapchainMETA. A minimal EGL context is created
 *        solely for the glReadPixels readback. The extension requires acquire to
 *        be called between xrBeginFrame and xrEndFrame, so the plugin runs a
 *        minimal frame loop (no swapchain submission — session stays in
 *        XR_SESSION_STATE_SYNCHRONIZED without ever calling xrEndFrame with
 *        layers). Depth pose and FOV come directly from XrEnvironmentDepthImageMETA
 *        — no separate xrLocateSpace call needed for depth.
 *        Depth format: R16F (float16), 2 bytes/pixel, top-down, inverted NDC.
 *        depth_m = near_z / r_value.
 *
 * Initialisation order (encoder before camera, EGL before depth provider)
 * -----------------------------------------------------------------------
 *   1. init_openxr()   — instance, session, reference spaces, depth extension
 *   2. init_egl()      — minimal EGL context for depth texture readback
 *   3. init_encoder()  — MediaCodec HEVC, creates input ANativeWindow
 *   4. init_camera()   — Camera2, targeting encoder's ANativeWindow
 *
 * Configuration (env vars, set via Unity JNI bridge before plugin start)
 * -----------------------------------------------------------------------
 *   ILLIXR_CAPTURE_FPS         Encoder target FPS hint     (default: 30)
 *   ILLIXR_ENCODER_BITRATE_BPS HEVC bitrate in bits/s      (default: 5000000)
 *   ILLIXR_ENCODER_IFRAME_SEC  IDR period in seconds       (default: 1)
 *   ILLIXR_DEPTH_DISABLED      Set to 1 to skip depth      (default: 0)
 *   ILLIXR_MAX_DEPTH_M         Far-depth cap in metres     (default: 0)
 *
 * Publishes: "sensor_frame" (ILLIXR::data_format::sensor_frame)
 */
class xr_sensor_capture : public threadloop {
public:
    [[maybe_unused]] xr_sensor_capture(const std::string& name, phonebook* pb);
    ~xr_sensor_capture() override;

protected:
    void        _p_one_iteration() override;
    skip_option _p_should_skip() override;

private:
    // ---- Configuration ----
    uint8_t capture_fps_{2};
    int32_t bitrate_bps_ = 5'000'000;
    int32_t iframe_sec_  = 1;

    // ---- Timing ----
    int64_t tick_interval_ns_ = 33'333'333LL; // ~30 Hz default
    int64_t last_tick_ns_     = 0;

    // ---- OpenXR ----
    bool init_openxr();
    void destroy_openxr();

    // Pump the XR event loop and handle session state transitions.
    // Returns true while the session is usable.
    bool pump_xr_events();

    XrInstance xr_instance_     = XR_NULL_HANDLE;
    XrSession  xr_session_      = XR_NULL_HANDLE;
    XrSpace    head_space_      = XR_NULL_HANDLE; //!< VIEW space (tracks head)
    XrSpace    local_space_     = XR_NULL_HANDLE; //!< LOCAL space (world-locked)
    bool       session_running_ = false;

    // XR_META_environment_depth function pointers
    // All defined in standard openxr.h — no Meta SDK headers needed.
    PFN_xrCreateEnvironmentDepthProviderMETA           xr_create_depth_provider_   = nullptr;
    PFN_xrDestroyEnvironmentDepthProviderMETA          xr_destroy_depth_provider_  = nullptr;
    PFN_xrStartEnvironmentDepthProviderMETA            xr_start_depth_provider_    = nullptr;
    PFN_xrCreateEnvironmentDepthSwapchainMETA          xr_create_depth_swapchain_  = nullptr;
    PFN_xrDestroyEnvironmentDepthSwapchainMETA         xr_destroy_depth_swapchain_ = nullptr;
    PFN_xrEnumerateEnvironmentDepthSwapchainImagesMETA xr_enum_depth_images_       = nullptr;
    PFN_xrGetEnvironmentDepthSwapchainStateMETA        xr_get_depth_state_         = nullptr;
    PFN_xrAcquireEnvironmentDepthImageMETA             xr_acquire_depth_image_     = nullptr;

    XrEnvironmentDepthProviderMETA  depth_provider_      = XR_NULL_HANDLE;
    XrEnvironmentDepthSwapchainMETA depth_swapchain_     = XR_NULL_HANDLE;
    bool                            depth_ext_available_ = false;

    // ---- Pose lookup via xrLocateSpace (RGB only) ----
    // Converts XrPosef to a row-major 4x4 float matrix in out_matrix[16].
    bool get_pose_at_timestamp(XrTime timestamp, float out_matrix[16]) const;

    // ---- EGL context for depth texture readback ----
    bool init_egl();
    void destroy_egl();

    EGLDisplay egl_display_ = EGL_NO_DISPLAY;
    EGLContext egl_context_ = EGL_NO_CONTEXT;
    EGLSurface egl_surface_ = EGL_NO_SURFACE; //!< 1x1 pbuffer, never rendered to

    // OpenGL ES texture handles — one per swapchain image slot.
    std::vector<GLuint> depth_textures_;

    // ---- Depth acquisition ----
    // Acquires the latest depth frame, reads it back to CPU via glReadPixels,
    // and populates the output parameters. Must be called between
    // xrBeginFrame and xrEndFrame.
    bool acquire_depth_frame(std::vector<uint8_t>& r16_out, data_format::camera_intrinsics& intrinsics_out, float& near_z_out,
                             float& far_z_out, float& tan_left_out, float& tan_right_out, float& tan_top_out,
                             float& tan_down_out, float depth_pose_out[16], XrTime& timestamp_out);

    // ---- MediaCodec HEVC encoder (Surface-input mode) ----
    bool init_encoder(int32_t width, int32_t height);
    void destroy_encoder();
    void drain_encoder_output();

    AMediaCodec*   codec_          = nullptr;
    AMediaFormat*  codec_format_   = nullptr;
    ANativeWindow* encoder_window_ = nullptr;
    int32_t        enc_width_      = 0;
    int32_t        enc_height_     = 0;

    // ---- Camera2 RGB ----
    bool init_camera();
    void destroy_camera();

    ACameraManager*                 camera_mgr_               = nullptr;
    ACameraDevice*                  camera_device_            = nullptr;
    ACameraCaptureSession*          capture_session_          = nullptr;
    ACaptureRequest*                capture_request_          = nullptr;
    ACameraOutputTarget*            camera_output_target_     = nullptr;
    ACaptureSessionOutputContainer* session_output_container_ = nullptr;
    ACaptureSessionOutput*          session_output_           = nullptr;

    data_format::camera_intrinsics rgb_intrinsics_;
    bool                           rgb_intrinsics_valid_ = false;

    // ---- Pending encoded RGB frames ----
    struct pending_rgb {
        std::vector<uint8_t> encoded;
        XrTime               timestamp = 0; // CLOCK_BOOTTIME ns
    };

    std::vector<pending_rgb> pending_frames_;

    // ---- Switchboard output ----
    const std::shared_ptr<switchboard>                      switchboard_;
    switchboard::network_writer<data_format::semantic_data> writer_;
    int32_t                                                 frame_number_ = 0;
    float                                                   max_depth_m_;
};

} // namespace ILLIXR
