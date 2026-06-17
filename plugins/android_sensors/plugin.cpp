#include "plugin.hpp"

#include "illixr/error_util.hpp"

#include <algorithm>
#include <android/log.h>
#include <camera/NdkCameraMetadataTags.h>
#include <cmath>
#include <cstring>
#include <media/NdkMediaError.h>
#include <stdexcept>
#include <string>

static constexpr const char* HEVC_MIME        = "video/hevc";
static constexpr int64_t     CODEC_TIMEOUT_US = 0LL; // non-blocking drain

using namespace ILLIXR;
using namespace ILLIXR::data_format;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static XrTime clock_boottime_xr() {
    struct timespec ts{};
    clock_gettime(CLOCK_BOOTTIME, &ts);
    return static_cast<XrTime>(static_cast<int64_t>(ts.tv_sec) * 1'000'000'000LL + ts.tv_nsec);
}

// ---------------------------------------------------------------------------
// Constructor / destructor
// ---------------------------------------------------------------------------

[[maybe_unused]] xr_sensor_capture::xr_sensor_capture(const std::string& name, phonebook* pb)
    : threadloop{name, pb}
    , switchboard_{phonebook_->lookup_impl<switchboard>()}
    , writer_{switchboard_->get_network_writer<semantic_data>("semantic_data", {})} {
    capture_fps_      = switchboard_->get_env_int("ILLIXR_CAPTURE_FPS", 2);
    bitrate_bps_      = switchboard_->get_env_int("ILLIXR_ENCODER_BITRATE_BPS", 5'000'000);
    iframe_sec_       = switchboard_->get_env_int("ILLIXR_ENCODER_IFRAME_SEC", 1);
    max_depth_m_      = switchboard_->get_env_float("ILLIXR_CAPTURE_MAX_DEPTH", 0.f);
    tick_interval_ns_ = static_cast<int64_t>(1'000'000'000 / capture_fps_);

    spdlog::get("illixr")->info("xr_sensor_capture init: fps={} bitrate={} iframe_sec={} max_depth={} ", capture_fps_,
                                bitrate_bps_, iframe_sec_, max_depth_m_);
    if (!init_openxr())
        throw std::runtime_error{"xr_sensor_capture: OpenXR init failed"};

    if (!init_egl())
        throw std::runtime_error{"xr_sensor_capture: EGL init failed"};

    constexpr int32_t CAMERA_W = 1280;
    constexpr int32_t CAMERA_H = 960;
    if (!init_encoder(CAMERA_W, CAMERA_H))
        throw std::runtime_error{"xr_sensor_capture: encoder init failed"};

    if (!init_camera())
        throw std::runtime_error{"xr_sensor_capture: Camera2 init failed"};
}

xr_sensor_capture::~xr_sensor_capture() {
    destroy_camera();
    destroy_encoder();
    destroy_egl();
    destroy_openxr();
}

// ---------------------------------------------------------------------------
// OpenXR
// ---------------------------------------------------------------------------

bool xr_sensor_capture::init_openxr() {
    const char* extensions[] = {
        "XR_KHR_android_create_instance",
        "XR_META_environment_depth",
    };

    XrInstanceCreateInfo inst_ci{XR_TYPE_INSTANCE_CREATE_INFO};
    inst_ci.enabledExtensionCount = 2u;
    inst_ci.enabledExtensionNames = extensions;
    std::strncpy(inst_ci.applicationInfo.applicationName, "ILLIXR_xr_sensor_capture", XR_MAX_APPLICATION_NAME_SIZE - 1);
    inst_ci.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;

    XrResult result = xrCreateInstance(&inst_ci, &xr_instance_);
    if (XR_FAILED(result)) {
        spdlog::get("illixr")->error("xrCreateInstance failed: {}", static_cast<int>(result));
        return false;
    }

    XrSystemGetInfo sys_gi{XR_TYPE_SYSTEM_GET_INFO};
    sys_gi.formFactor    = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrSystemId system_id = XR_NULL_SYSTEM_ID;
    result               = xrGetSystem(xr_instance_, &sys_gi, &system_id);
    if (XR_FAILED(result)) {
        spdlog::get("illixr")->error("xrGetSystem failed: {}", static_cast<int>(result));
        return false;
    }

    // Session without graphics binding — we don't submit rendered frames.
    // The session stays in SYNCHRONIZED state; we never call xrEndFrame
    // with layers, but we do call xrBeginFrame/xrEndFrame so that
    // xrAcquireEnvironmentDepthImageMETA is permitted.
    XrSessionCreateInfo sess_ci{XR_TYPE_SESSION_CREATE_INFO};
    sess_ci.systemId = system_id;
    result           = xrCreateSession(xr_instance_, &sess_ci, &xr_session_);
    if (XR_FAILED(result)) {
        spdlog::get("illixr")->error("xrCreateSession failed: {}", static_cast<int>(result));
        return false;
    }

    XrReferenceSpaceCreateInfo space_ci{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    space_ci.poseInReferenceSpace = {{0.f, 0.f, 0.f, 1.f}, {0.f, 0.f, 0.f}};

    space_ci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    xrCreateReferenceSpace(xr_session_, &space_ci, &head_space_);

    space_ci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    xrCreateReferenceSpace(xr_session_, &space_ci, &local_space_);

    // Load all XR_META_environment_depth entry points from openxr.h PFN types.
    // These are part of the standard Khronos SDK — no Meta SDK headers needed.
    auto load = [&](const char* fname, PFN_xrVoidFunction* out) {
        return xrGetInstanceProcAddr(xr_instance_, fname, out) == XR_SUCCESS;
    };

    bool ok = true;
    ok &= load("xrCreateEnvironmentDepthProviderMETA", reinterpret_cast<PFN_xrVoidFunction*>(&xr_create_depth_provider_));
    ok &= load("xrDestroyEnvironmentDepthProviderMETA", reinterpret_cast<PFN_xrVoidFunction*>(&xr_destroy_depth_provider_));
    ok &= load("xrStartEnvironmentDepthProviderMETA", reinterpret_cast<PFN_xrVoidFunction*>(&xr_start_depth_provider_));
    ok &= load("xrCreateEnvironmentDepthSwapchainMETA", reinterpret_cast<PFN_xrVoidFunction*>(&xr_create_depth_swapchain_));
    ok &= load("xrDestroyEnvironmentDepthSwapchainMETA", reinterpret_cast<PFN_xrVoidFunction*>(&xr_destroy_depth_swapchain_));
    ok &= load("xrEnumerateEnvironmentDepthSwapchainImagesMETA", reinterpret_cast<PFN_xrVoidFunction*>(&xr_enum_depth_images_));
    ok &= load("xrGetEnvironmentDepthSwapchainStateMETA", reinterpret_cast<PFN_xrVoidFunction*>(&xr_get_depth_state_));
    ok &= load("xrAcquireEnvironmentDepthImageMETA", reinterpret_cast<PFN_xrVoidFunction*>(&xr_acquire_depth_image_));

    if (!ok) {
        spdlog::get("illixr")->warn("XR_META_environment_depth entry points missing — depth disabled");
        depth_ext_available_ = false;
    } else {
        depth_ext_available_ = true;
        spdlog::get("illixr")->info("XR_META_environment_depth loaded");
    }

    return true;
}

bool xr_sensor_capture::pump_xr_events() {
    XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
    while (xrPollEvent(xr_instance_, &event) == XR_SUCCESS) {
        if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            auto* state_event = reinterpret_cast<XrEventDataSessionStateChanged*>(&event);
            switch (state_event->state) {
            case XR_SESSION_STATE_READY: {
                XrSessionBeginInfo begin_info{XR_TYPE_SESSION_BEGIN_INFO};
                begin_info.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                xrBeginSession(xr_session_, &begin_info);
                session_running_ = true;
                spdlog::get("illixr")->info("XR session running");
            } break;
            case XR_SESSION_STATE_STOPPING:
                xrEndSession(xr_session_);
                session_running_ = false;
                break;
            case XR_SESSION_STATE_EXITING:
            case XR_SESSION_STATE_LOSS_PENDING:
                return false;
            default:
                break;
            }
        }
        event = {XR_TYPE_EVENT_DATA_BUFFER};
    }
    return true;
}

void xr_sensor_capture::destroy_openxr() {
    if (depth_swapchain_ != XR_NULL_HANDLE && xr_destroy_depth_swapchain_) {
        xr_destroy_depth_swapchain_(depth_swapchain_);
        depth_swapchain_ = XR_NULL_HANDLE;
    }
    if (depth_provider_ != XR_NULL_HANDLE && xr_destroy_depth_provider_) {
        xr_destroy_depth_provider_(depth_provider_);
        depth_provider_ = XR_NULL_HANDLE;
    }
    if (head_space_ != XR_NULL_HANDLE) {
        xrDestroySpace(head_space_);
        head_space_ = XR_NULL_HANDLE;
    }
    if (local_space_ != XR_NULL_HANDLE) {
        xrDestroySpace(local_space_);
        local_space_ = XR_NULL_HANDLE;
    }
    if (xr_session_ != XR_NULL_HANDLE) {
        xrDestroySession(xr_session_);
        xr_session_ = XR_NULL_HANDLE;
    }
    if (xr_instance_ != XR_NULL_HANDLE) {
        xrDestroyInstance(xr_instance_);
        xr_instance_ = XR_NULL_HANDLE;
    }
}

// ---------------------------------------------------------------------------
// EGL — minimal context for depth texture readback only
// ---------------------------------------------------------------------------

bool xr_sensor_capture::init_egl() {
    egl_display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (egl_display_ == EGL_NO_DISPLAY) {
        spdlog::get("illixr")->error("eglGetDisplay failed");
        return false;
    }

    EGLint major = 0, minor = 0;
    if (!eglInitialize(egl_display_, &major, &minor)) {
        spdlog::get("illixr")->error("eglInitialize failed");
        return false;
    }

    // Minimal config: we only need a context, not a real render surface.
    const EGLint config_attribs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_NONE};
    EGLConfig    egl_config       = nullptr;
    EGLint       num_configs      = 0;
    if (!eglChooseConfig(egl_display_, config_attribs, &egl_config, 1, &num_configs) || num_configs == 0) {
        spdlog::get("illixr")->error("eglChooseConfig failed");
        return false;
    }

    // 1×1 pbuffer — never rendered to, exists only to make the context current.
    const EGLint pbuffer_attribs[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
    egl_surface_                   = eglCreatePbufferSurface(egl_display_, egl_config, pbuffer_attribs);
    if (egl_surface_ == EGL_NO_SURFACE) {
        spdlog::get("illixr")->error("eglCreatePbufferSurface failed");
        return false;
    }

    const EGLint ctx_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    egl_context_               = eglCreateContext(egl_display_, egl_config, EGL_NO_CONTEXT, ctx_attribs);
    if (egl_context_ == EGL_NO_CONTEXT) {
        spdlog::get("illixr")->error("eglCreateContext failed");
        return false;
    }

    if (!eglMakeCurrent(egl_display_, egl_surface_, egl_surface_, egl_context_)) {
        spdlog::get("illixr")->error("eglMakeCurrent failed");
        return false;
    }

    spdlog::get("illixr")->info("EGL context created (OpenGL ES 3.x)");
    return true;
}

void xr_sensor_capture::destroy_egl() {
    if (!depth_textures_.empty()) {
        glDeleteTextures(static_cast<GLsizei>(depth_textures_.size()), depth_textures_.data());
        depth_textures_.clear();
    }
    if (egl_display_ != EGL_NO_DISPLAY) {
        eglMakeCurrent(egl_display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (egl_context_ != EGL_NO_CONTEXT) {
            eglDestroyContext(egl_display_, egl_context_);
            egl_context_ = EGL_NO_CONTEXT;
        }
        if (egl_surface_ != EGL_NO_SURFACE) {
            eglDestroySurface(egl_display_, egl_surface_);
            egl_surface_ = EGL_NO_SURFACE;
        }
        eglTerminate(egl_display_);
        egl_display_ = EGL_NO_DISPLAY;
    }
}

// ---------------------------------------------------------------------------
// Pose lookup (RGB only — depth pose comes from XrEnvironmentDepthImageMETA)
// ---------------------------------------------------------------------------

bool xr_sensor_capture::get_pose_at_timestamp(XrTime timestamp, float out_matrix[16]) const {
    if (timestamp <= 0)
        return false;

    XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
    XrResult        result = xrLocateSpace(head_space_, local_space_, timestamp, &location);
    if (XR_FAILED(result)) {
        spdlog::get("illixr")->warn("xrLocateSpace failed: {}", static_cast<int>(result));
        return false;
    }

    const XrSpaceLocationFlags required = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    if ((location.locationFlags & required) != required) {
        spdlog::get("illixr")->warn("xrLocateSpace pose not valid (flags=0x{})", static_cast<unsigned>(location.locationFlags));
        return false;
    }

    const float x  = location.pose.orientation.x;
    const float y  = location.pose.orientation.y;
    const float z  = location.pose.orientation.z;
    const float w  = location.pose.orientation.w;
    const float tx = location.pose.position.x;
    const float ty = location.pose.position.y;
    const float tz = location.pose.position.z;

    out_matrix[0] = 1.f - 2.f * (y * y + z * z);
    out_matrix[1] = 2.f * (x * y - w * z);
    out_matrix[2] = 2.f * (x * z + w * y);
    out_matrix[3] = tx;

    out_matrix[4] = 2.f * (x * y + w * z);
    out_matrix[5] = 1.f - 2.f * (x * x + z * z);
    out_matrix[6] = 2.f * (y * z - w * x);
    out_matrix[7] = ty;

    out_matrix[8]  = 2.f * (x * z - w * y);
    out_matrix[9]  = 2.f * (y * z + w * x);
    out_matrix[10] = 1.f - 2.f * (x * x + y * y);
    out_matrix[11] = tz;

    out_matrix[12] = 0.f;
    out_matrix[13] = 0.f;
    out_matrix[14] = 0.f;
    out_matrix[15] = 1.f;

    return true;
}

// ---------------------------------------------------------------------------
// Depth acquisition via XR_META_environment_depth
// ---------------------------------------------------------------------------

bool xr_sensor_capture::acquire_depth_frame(std::vector<uint8_t>& r16_out, camera_intrinsics& intrinsics_out, float& near_z_out,
                                            float& far_z_out, float& tan_left_out, float& tan_right_out, float& tan_top_out,
                                            float& tan_down_out, float depth_pose_out[16], XrTime& timestamp_out) {
    if (!depth_ext_available_ || depth_provider_ == XR_NULL_HANDLE)
        return false;

    // Lazy depth swapchain init: deferred until session is running because
    // xrCreateEnvironmentDepthSwapchainMETA requires an active session.
    if (depth_swapchain_ == XR_NULL_HANDLE) {
        XrEnvironmentDepthSwapchainCreateInfoMETA sc_ci{XR_TYPE_ENVIRONMENT_DEPTH_SWAPCHAIN_CREATE_INFO_META};
        sc_ci.createFlags = 0;
        XrResult result   = xr_create_depth_swapchain_(depth_provider_, &sc_ci, &depth_swapchain_);
        if (XR_FAILED(result)) {
            spdlog::get("illixr")->error("xrCreateEnvironmentDepthSwapchainMETA failed: {}", static_cast<int>(result));
            return false;
        }

        // Query swapchain dimensions.
        XrEnvironmentDepthSwapchainStateMETA state{XR_TYPE_ENVIRONMENT_DEPTH_SWAPCHAIN_STATE_META};
        xr_get_depth_state_(depth_swapchain_, &state);
        spdlog::get("illixr")->info("Depth swapchain: {}x{}", state.width, state.height);

        // Enumerate OpenGL ES texture handles.
        uint32_t img_count = 0;
        xr_enum_depth_images_(depth_swapchain_, 0, &img_count, nullptr);

        std::vector<XrSwapchainImageOpenGLESKHR> images(img_count, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
        xr_enum_depth_images_(depth_swapchain_, img_count, &img_count,
                              reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data()));

        depth_textures_.resize(img_count);
        for (uint32_t i = 0; i < img_count; ++i)
            depth_textures_[i] = images[i].image;

        spdlog::get("illixr")->info("Depth swapchain has {} image slots", img_count);
    }

    // Acquire the latest depth image.
    // Must be called between xrBeginFrame and xrEndFrame (enforced by caller).
    XrEnvironmentDepthImageAcquireInfoMETA acq_info{XR_TYPE_ENVIRONMENT_DEPTH_IMAGE_ACQUIRE_INFO_META};
    acq_info.space       = local_space_;
    acq_info.displayTime = clock_boottime_xr();

    XrEnvironmentDepthImageMETA depth_image{XR_TYPE_ENVIRONMENT_DEPTH_IMAGE_META};
    depth_image.views[0] = {XR_TYPE_ENVIRONMENT_DEPTH_IMAGE_VIEW_META};
    depth_image.views[1] = {XR_TYPE_ENVIRONMENT_DEPTH_IMAGE_VIEW_META};

    XrResult result = xr_acquire_depth_image_(depth_provider_, &acq_info, &depth_image);

    // XR_ENVIRONMENT_DEPTH_NOT_AVAILABLE_META is a success code meaning
    // the provider hasn't produced a frame yet — not an error.
    if (result == XR_ENVIRONMENT_DEPTH_NOT_AVAILABLE_META)
        return false;
    if (XR_FAILED(result)) {
        spdlog::get("illixr")->warn("xrAcquireEnvironmentDepthImageMETA failed: {}", static_cast<int>(result));
        return false;
    }

    near_z_out = depth_image.nearZ;
    far_z_out  = depth_image.farZ;

    // Use left-eye view (index 0) — single depth sensor, left camera.
    const XrEnvironmentDepthImageViewMETA& view = depth_image.views[0];

    // XrFovf uses radians; store tangents (matching the existing server convention).
    // angleLeft and angleDown are negative (left/down of optical axis).
    tan_left_out  = std::tan(view.fov.angleLeft); // negative
    tan_right_out = std::tan(view.fov.angleRight);
    tan_top_out   = std::tan(view.fov.angleUp);
    tan_down_out  = std::tan(view.fov.angleDown); // negative

    // Derive pinhole intrinsics from FOV tangents.
    // Query swapchain dimensions for width/height.
    XrEnvironmentDepthSwapchainStateMETA state{XR_TYPE_ENVIRONMENT_DEPTH_SWAPCHAIN_STATE_META};
    xr_get_depth_state_(depth_swapchain_, &state);
    const auto w_f = static_cast<float>(state.width);
    const auto h_f = static_cast<float>(state.height);

    const float abs_left  = std::abs(tan_left_out);
    const float abs_right = std::abs(tan_right_out);
    const float abs_top   = std::abs(tan_top_out);
    const float abs_down  = std::abs(tan_down_out);

    intrinsics_out.fx     = w_f / (abs_right + abs_left);
    intrinsics_out.fy     = h_f / (abs_top + abs_down);
    intrinsics_out.cx     = abs_left * intrinsics_out.fx;
    intrinsics_out.cy     = abs_top * intrinsics_out.fy;
    intrinsics_out.width  = static_cast<int32_t>(state.width);
    intrinsics_out.height = static_cast<int32_t>(state.height);

    // Depth pose comes from the acquire result directly — no xrLocateSpace needed.
    // Convert XrPosef to row-major 4x4 matrix.
    const float qx   = view.pose.orientation.x;
    const float qy   = view.pose.orientation.y;
    const float qz   = view.pose.orientation.z;
    const float qw   = view.pose.orientation.w;
    const float tx   = view.pose.position.x;
    const float ty_p = view.pose.position.y;
    const float tz   = view.pose.position.z;

    depth_pose_out[0]  = 1.f - 2.f * (qy * qy + qz * qz);
    depth_pose_out[1]  = 2.f * (qx * qy - qw * qz);
    depth_pose_out[2]  = 2.f * (qx * qz + qw * qy);
    depth_pose_out[3]  = tx;
    depth_pose_out[4]  = 2.f * (qx * qy + qw * qz);
    depth_pose_out[5]  = 1.f - 2.f * (qx * qx + qz * qz);
    depth_pose_out[6]  = 2.f * (qy * qz - qw * qx);
    depth_pose_out[7]  = ty_p;
    depth_pose_out[8]  = 2.f * (qx * qz - qw * qy);
    depth_pose_out[9]  = 2.f * (qy * qz + qw * qx);
    depth_pose_out[10] = 1.f - 2.f * (qx * qx + qy * qy);
    depth_pose_out[11] = tz;
    depth_pose_out[12] = 0.f;
    depth_pose_out[13] = 0.f;
    depth_pose_out[14] = 0.f;
    depth_pose_out[15] = 1.f;

    // GPU readback: bind the swapchain texture and read pixels to CPU.
    // Format is R16F (GL_R16F), 2 bytes/pixel, matching the expected R16_SFloat.
    const GLuint  tex = depth_textures_[depth_image.swapchainIndex];
    const int32_t w   = intrinsics_out.width;
    const int32_t h   = intrinsics_out.height;

    // Bind to a framebuffer for glReadPixels.
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);

    r16_out.resize(static_cast<size_t>(w * h * 2));
    // GL_RED + GL_HALF_FLOAT reads R16F as 2 bytes/pixel.
    glReadPixels(0, 0, w, h, GL_RED, GL_HALF_FLOAT, r16_out.data());

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);

    // glReadPixels returns bottom-up (OpenGL convention); flip to top-down
    // to match the server's expectation and the original Unity behaviour.
    const auto         row_bytes = static_cast<size_t>(w * 2);
    std::vector<uint8_t> row_buf(row_bytes);
    for (int32_t top = 0, bot = h - 1; top < bot; ++top, --bot) {
        uint8_t* row_top = r16_out.data() + top * row_bytes;
        uint8_t* row_bot = r16_out.data() + bot * row_bytes;
        std::memcpy(row_buf.data(), row_top, row_bytes);
        std::memcpy(row_top, row_bot, row_bytes);
        std::memcpy(row_bot, row_buf.data(), row_bytes);
    }

    // Timestamp: use the depth frame's pose time as the acquisition timestamp.
    timestamp_out = static_cast<XrTime>(acq_info.displayTime);

    return true;
}

// ---------------------------------------------------------------------------
// MediaCodec encoder — Surface-input mode
// ---------------------------------------------------------------------------

bool xr_sensor_capture::init_encoder(int32_t width, int32_t height) {
    codec_ = AMediaCodec_createEncoderByType(HEVC_MIME);
    if (codec_ == nullptr) {
        spdlog::get("illixr")->error("AMediaCodec_createEncoderByType failed");
        return false;
    }

    codec_format_ = AMediaFormat_new();
    AMediaFormat_setString(codec_format_, AMEDIAFORMAT_KEY_MIME, HEVC_MIME);
    AMediaFormat_setInt32(codec_format_, AMEDIAFORMAT_KEY_WIDTH, width);
    AMediaFormat_setInt32(codec_format_, AMEDIAFORMAT_KEY_HEIGHT, height);
    AMediaFormat_setInt32(codec_format_, AMEDIAFORMAT_KEY_BIT_RATE, bitrate_bps_);
    AMediaFormat_setInt32(codec_format_, AMEDIAFORMAT_KEY_FRAME_RATE, static_cast<int32_t>(capture_fps_));
    AMediaFormat_setInt32(codec_format_, AMEDIAFORMAT_KEY_I_FRAME_INTERVAL, iframe_sec_);
    // COLOR_FormatSurface — required for Surface-input mode.
    AMediaFormat_setInt32(codec_format_, AMEDIAFORMAT_KEY_COLOR_FORMAT, 0x7F000789);

    if (AMediaCodec_configure(codec_, codec_format_, nullptr, nullptr, 1) != AMEDIA_OK) {
        spdlog::get("illixr")->error("AMediaCodec_configure failed");
        destroy_encoder();
        return false;
    }

    if (AMediaCodec_createInputSurface(codec_, &encoder_window_) != AMEDIA_OK || encoder_window_ == nullptr) {
        spdlog::get("illixr")->error("AMediaCodec_createInputSurface failed");
        destroy_encoder();
        return false;
    }

    if (AMediaCodec_start(codec_) != AMEDIA_OK) {
        spdlog::get("illixr")->error("AMediaCodec_start failed");
        destroy_encoder();
        return false;
    }

    enc_width_  = width;
    enc_height_ = height;
    spdlog::get("illixr")->info("MediaCodec HEVC encoder started (Surface-input): {}x{}", width, height);
    return true;
}

void xr_sensor_capture::destroy_encoder() {
    if (codec_ != nullptr) {
        AMediaCodec_stop(codec_);
        AMediaCodec_delete(codec_);
        codec_ = nullptr;
    }
    encoder_window_ = nullptr; // owned by codec
    if (codec_format_ != nullptr) {
        AMediaFormat_delete(codec_format_);
        codec_format_ = nullptr;
    }
    enc_width_ = enc_height_ = 0;
}

void xr_sensor_capture::drain_encoder_output() {
    if (codec_ == nullptr)
        return;

    while (true) {
        AMediaCodecBufferInfo info{};
        ssize_t               idx = AMediaCodec_dequeueOutputBuffer(codec_, &info, CODEC_TIMEOUT_US);

        if (idx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED)
            continue;
        if (idx == AMEDIACODEC_INFO_TRY_AGAIN_LATER || idx < 0)
            break;

        // Skip SPS/PPS codec config packets — embedded in the annexb stream.
        if (!(info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) && info.size > 0) {
            size_t         buf_size = 0;
            const uint8_t* buf      = AMediaCodec_getOutputBuffer(codec_, static_cast<size_t>(idx), &buf_size);
            if (buf != nullptr) {
                pending_rgb frame{};
                frame.encoded.assign(buf + info.offset, buf + info.offset + info.size);
                // presentationTimeUs is CLOCK_BOOTTIME microseconds → ns
                frame.timestamp = static_cast<XrTime>(info.presentationTimeUs * 1'000LL);
                pending_frames_.push_back(std::move(frame));
            }
        }
        AMediaCodec_releaseOutputBuffer(codec_, static_cast<size_t>(idx), false);
    }
}

// ---------------------------------------------------------------------------
// Camera2
// ---------------------------------------------------------------------------

static void on_camera_disconnected(void*, ACameraDevice* dev) {
    __android_log_print(ANDROID_LOG_ERROR, "xr_sensor_capture", "Camera disconnected: %p", static_cast<void*>(dev));
}

static void on_camera_error(void*, ACameraDevice* dev, int error) {
    __android_log_print(ANDROID_LOG_ERROR, "xr_sensor_capture", "Camera error %d on %p", error, static_cast<void*>(dev));
}

static void on_session_active(void*, ACameraCaptureSession*) {
    spdlog::get("illixr")->info("Capture session active");
}

static void on_session_ready(void*, ACameraCaptureSession*) {
    spdlog::get("illixr")->info("Capture session ready");
}

static void on_session_closed(void*, ACameraCaptureSession*) {
    spdlog::get("illixr")->info("Capture session closed");
}

bool xr_sensor_capture::init_camera() {
    camera_mgr_ = ACameraManager_create();
    if (camera_mgr_ == nullptr) {
        spdlog::get("illixr")->error("ACameraManager_create failed");
        return false;
    }

    ACameraIdList* id_list = nullptr;
    if (ACameraManager_getCameraIdList(camera_mgr_, &id_list) != ACAMERA_OK || id_list == nullptr || id_list->numCameras == 0) {
        spdlog::get("illixr")->error("No cameras found");
        return false;
    }

    const char* selected_id = nullptr;
    for (int i = 0; i < id_list->numCameras; ++i) {
        ACameraMetadata* meta = nullptr;
        ACameraManager_getCameraCharacteristics(camera_mgr_, id_list->cameraIds[i], &meta);
        ACameraMetadata_const_entry entry{};
        ACameraMetadata_getConstEntry(meta, ACAMERA_LENS_FACING, &entry);
        if (entry.data.u8[0] == ACAMERA_LENS_FACING_BACK) {
            selected_id = id_list->cameraIds[i];
            ACameraMetadata_free(meta);
            break;
        }
        ACameraMetadata_free(meta);
    }
    if (selected_id == nullptr) {
        spdlog::get("illixr")->warn("No back-facing camera; falling back to camera 0");
        selected_id = id_list->cameraIds[0];
    }

    // Cache intrinsics.
    ACameraMetadata* meta = nullptr;
    ACameraManager_getCameraCharacteristics(camera_mgr_, selected_id, &meta);
    if (meta != nullptr) {
        ACameraMetadata_const_entry intr{};
        if (ACameraMetadata_getConstEntry(meta, ACAMERA_LENS_INTRINSIC_CALIBRATION, &intr) == ACAMERA_OK && intr.count >= 5) {
            rgb_intrinsics_.fx    = intr.data.f[0];
            rgb_intrinsics_.fy    = intr.data.f[1];
            rgb_intrinsics_.cx    = intr.data.f[2];
            rgb_intrinsics_.cy    = intr.data.f[3];
            rgb_intrinsics_valid_ = (rgb_intrinsics_.fx > 0.f);
        }
        ACameraMetadata_free(meta);
    }
    rgb_intrinsics_.width  = enc_width_;
    rgb_intrinsics_.height = enc_height_;

    ACameraDevice_StateCallbacks device_cbs{};
    device_cbs.onDisconnected = on_camera_disconnected;
    device_cbs.onError        = on_camera_error;
    if (ACameraManager_openCamera(camera_mgr_, selected_id, &device_cbs, &camera_device_) != ACAMERA_OK) {
        spdlog::get("illixr")->error("ACameraManager_openCamera failed");
        ACameraManager_deleteCameraIdList(id_list);
        return false;
    }
    ACameraManager_deleteCameraIdList(id_list);

    ACaptureSessionOutputContainer_create(&session_output_container_);
    ACaptureSessionOutput_create(encoder_window_, &session_output_);
    ACaptureSessionOutputContainer_add(session_output_container_, session_output_);
    ACameraOutputTarget_create(encoder_window_, &camera_output_target_);

    ACameraDevice_createCaptureRequest(camera_device_, TEMPLATE_PREVIEW, &capture_request_);
    ACaptureRequest_addTarget(capture_request_, camera_output_target_);

    ACameraCaptureSession_stateCallbacks session_cbs{};
    session_cbs.onActive = on_session_active;
    session_cbs.onReady  = on_session_ready;
    session_cbs.onClosed = on_session_closed;

    if (ACameraDevice_createCaptureSession(camera_device_, session_output_container_, &session_cbs, &capture_session_) !=
        ACAMERA_OK) {
        spdlog::get("illixr")->error("ACameraDevice_createCaptureSession failed");
        return false;
    }

    ACameraCaptureSession_setRepeatingRequest(capture_session_, nullptr, 1, &capture_request_, nullptr);
    spdlog::get("illixr")->info("Camera2 → encoder surface: {}x{}", enc_width_, enc_height_);
    return true;
}

void xr_sensor_capture::destroy_camera() {
    if (capture_session_ != nullptr) {
        ACameraCaptureSession_stopRepeating(capture_session_);
        ACameraCaptureSession_close(capture_session_);
        capture_session_ = nullptr;
    }
    if (capture_request_ != nullptr) {
        ACaptureRequest_free(capture_request_);
        capture_request_ = nullptr;
    }
    if (camera_output_target_ != nullptr) {
        ACameraOutputTarget_free(camera_output_target_);
        camera_output_target_ = nullptr;
    }
    if (session_output_ != nullptr) {
        ACaptureSessionOutput_free(session_output_);
        session_output_ = nullptr;
    }
    if (session_output_container_ != nullptr) {
        ACaptureSessionOutputContainer_free(session_output_container_);
        session_output_container_ = nullptr;
    }
    if (camera_device_ != nullptr) {
        ACameraDevice_close(camera_device_);
        camera_device_ = nullptr;
    }
    if (camera_mgr_ != nullptr) {
        ACameraManager_delete(camera_mgr_);
        camera_mgr_ = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Threadloop
// ---------------------------------------------------------------------------

threadloop::skip_option xr_sensor_capture::_p_should_skip() {
    if (clock_boottime_xr() - last_tick_ns_ < tick_interval_ns_)
        return skip_option::skip_and_yield;
    return skip_option::run;
}

void xr_sensor_capture::_p_one_iteration() {
    last_tick_ns_ = clock_boottime_xr();

    // Drive the XR session state machine.
    if (!pump_xr_events())
        return;

    // Depth acquisition must be bracketed by xrBeginFrame / xrEndFrame.
    // We always call the pair regardless of depth_disabled so the runtime
    // stays happy; we just skip the acquire if depth is disabled.
    XrFrameWaitInfo wait_info{XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState    frame_state{XR_TYPE_FRAME_STATE};

    if (session_running_) {
        xrWaitFrame(xr_session_, &wait_info, &frame_state);
        xrBeginFrame(xr_session_, nullptr);
    }

    // ---- Drain encoder output (RGB) ----
    drain_encoder_output();

    // ---- Depth ----
    camera_intrinsics    depth_intr{};
    std::vector<uint8_t> depth_data;
    float                depth_near_z = 0.f, depth_far_z = 0.f;
    float                tan_l = 0.f, tan_r = 0.f, tan_t = 0.f, tan_d = 0.f;
    float                depth_matrix[16]{};
    XrTime               depth_time = 0;
    bool                 has_depth  = false;

    if (session_running_ && depth_ext_available_) {
        // Lazily create and start the depth provider on first running frame.
        if (depth_provider_ == XR_NULL_HANDLE) {
            XrEnvironmentDepthProviderCreateInfoMETA prov_ci{XR_TYPE_ENVIRONMENT_DEPTH_PROVIDER_CREATE_INFO_META};
            prov_ci.createFlags = 0;
            XrResult r          = xr_create_depth_provider_(xr_session_, &prov_ci, &depth_provider_);
            if (XR_FAILED(r)) {
                spdlog::get("illixr")->error("xrCreateEnvironmentDepthProviderMETA failed: {}", static_cast<int>(r));
                depth_ext_available_ = false;
            } else {
                xr_start_depth_provider_(depth_provider_);
                spdlog::get("illixr")->info("Depth provider created and started");
            }
        }

        has_depth = acquire_depth_frame(depth_data, depth_intr, depth_near_z, depth_far_z, tan_l, tan_r, tan_t, tan_d,
                                        depth_matrix, depth_time);
    }

    // End the frame with no layers — session stays SYNCHRONIZED.
    if (session_running_) {
        XrFrameEndInfo end_info{XR_TYPE_FRAME_END_INFO};
        end_info.displayTime          = frame_state.predictedDisplayTime;
        end_info.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        end_info.layerCount           = 0;
        end_info.layers               = nullptr;
        xrEndFrame(xr_session_, &end_info);
    }

    // ---- Publish one semantic_data per pending encoded RGB frame ----
    for (auto& rgb : pending_frames_) {
        float rgb_matrix[16]{};
        if (!get_pose_at_timestamp(rgb.timestamp, rgb_matrix)) {
            spdlog::get("illixr")->warn("[frame={}] RGB pose lookup failed, dropping", frame_number_);
            frame_number_++;
            continue;
        }

        if (!has_depth) {
            spdlog::get("illixr")->warn("[frame={}] No depth available, dropping", frame_number_);
            frame_number_++;
            continue;
        }

        semantic_data frame{};
        frame.frame_number     = frame_number_++;
        frame.image            = std::move(rgb.encoded);
        frame.intrinsics       = rgb_intrinsics_;
        frame.rgb_timestamp_ns = static_cast<int64_t>(rgb.timestamp);
        frame.max_depth        = max_depth_m_;
        std::memcpy(frame.rgb_camera_pose, rgb_matrix, sizeof(rgb_matrix));

        if (has_depth) {
            frame.depth              = depth_data;
            frame.depth_near_z       = depth_near_z;
            frame.depth_intrinsics   = depth_intr;
            frame.depth_timestamp_ns = static_cast<int64_t>(depth_time);
            std::memcpy(frame.depth_pose, depth_matrix, sizeof(depth_matrix));
        }

        writer_.put(writer_.allocate<semantic_data>(std::move(frame)));
    }

    pending_frames_.clear();
}

// ---------------------------------------------------------------------------
// Plugin registration
// ---------------------------------------------------------------------------

PLUGIN_MAIN(xr_sensor_capture)
