/**
 *  A header declaring constants available through the 'const_registry' service.
 *
 *  See 'common/global_module_defs.hpp' for implementation details.
 *  Include via 'common/global_module_defs.hpp'.
 */
#pragma once


#define DATA_PATH DATA_PATH
/** @brief: Path string pointing to the data directory for SLAM and VIO. */
    DECLARE_CONST(DATA_PATH,                RawPath,    noop<RawPath>,  "/dev/null");

#define DEMO_OBJ_PATH DEMO_OBJ_PATH
/** @brief: Path string pointing to the data directory for the GL demo scene. */
    DECLARE_CONST(DEMO_OBJ_PATH,            RawPath,    noop<RawPath>,  "/dev/null");

#define OFFLOAD_PATH OFFLOAD_PATH
/** @brief: Path string pointing to the output directory for the offloaded textures. */
    DECLARE_CONST(OFFLOAD_PATH,             RawPath,    noop<RawPath>,  "/dev/null");

#define ALIGNMENT_MATRIX_PATH ALIGNMENT_MATRIX_PATH
/** @brief: Path string pointing to the pose lookup alignment matrix file. */
    DECLARE_CONST(ALIGNMENT_MATRIX_PATH,    RawPath,    noop<RawPath>,  "/dev/null");


#define GROUND_TRUTH_PATH_SUB GROUND_TRUTH_PATH_SUB
/** @brief: Path string pointing to the subpath for the ground truth estimate (not pathified: _SUB suffix). */
    DECLARE_CONST(GROUND_TRUTH_PATH_SUB,    RawPath,    noop<RawPath>,  "");


#define ENABLE_VERBOSE_ERRORS ENABLE_VERBOSE_ERRORS
/** @brief: Flag to activate verbose logging in 'common/error_util.hpp'. */
    DECLARE_CONST(ENABLE_VERBOSE_ERRORS,    bool,   to<bool>,   false);

#define ENABLE_PRE_SLEEP ENABLE_PRE_SLEEP
/** @brief: Flag to activate sleeping at application start for attaching gdb. Disables 'catchsegv'. */
    DECLARE_CONST(ENABLE_PRE_SLEEP,         bool,   to<bool>,   false);

#define ENABLE_OFFLOAD ENABLE_OFFLOAD
/** @brief: Flag to activate data collection at runtime. */
    DECLARE_CONST(ENABLE_OFFLOAD,           bool,   to<bool>,   false);

#define ENABLE_ALIGNMENT ENABLE_ALIGNMENT
/** @brief: Flag to activate the ground-truth alignment. */
    DECLARE_CONST(ENABLE_ALIGNMENT,         bool,   to<bool>,   false);

#define DISABLE_TIMEWARP DISABLE_TIMEWARP
/** @brief: Flag to disable warping in 'timewarp_gl'. */
    DECLARE_CONST(DISABLE_TIMEWARP,         bool,   to<bool>,   false);


#define RUN_DURATION RUN_DURATION
/** @brief: Integer specifying the application run duration in seconds. */
    DECLARE_CONST(RUN_DURATION,         long,           to<long>,           60L);  /// Seconds

#define PRE_SLEEP_DURATION PRE_SLEEP_DURATION
/** @brief: Integer specifying the sleep duration in seconds (see 'ENABLE_PRE_SLEEP'). */
    DECLARE_CONST(PRE_SLEEP_DURATION,   unsigned int,   to<unsigned int>,   10);   /// Seconds


#define FB_WIDTH FB_WIDTH
/** @brief: Integer specifying the framebuffer width in pixels. */
    DECLARE_CONST(FB_WIDTH,     int,    to<int>,    2560); /// Pixels

#define FB_HEIGHT FB_HEIGHT
/** @brief: Integer specifying the framebuffer heigth in pixels. */
    DECLARE_CONST(FB_HEIGHT,    int,    to<int>,    1440); /// Pixels

#define REFRESH_RATE REFRESH_RATE
/** @brief: Float specifying the refresh rate in Hz. */
    DECLARE_CONST(REFRESH_RATE, double, to<double>, 60.0); /// Hz


#define REALSENSE_CAM REALSENSE_CAM
/**
 * @brief: When realsense plugin is used, this argument selects the model to use.
 * Currently supports 'auto', 'D4XX' series that have IMU, and 'T26X'.
 * 'auto' will select device based on supported IMU streams or T26X presence, prefering D4XX series with IMU.
 */
    DECLARE_CONST(REALSENSE_CAM,    std::string_view,   noop<std::string_view>, "auto");
