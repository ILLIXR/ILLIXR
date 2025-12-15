#if defined(ILLIXR_ANDROID_BUILD) && defined(ENABLE_MONADO)
#else
#include "illixr.hpp"
#ifdef ILLIXR_ANDROID_BUILD
#include <android_native_app_glue.h>
#include <EGL/egl.h>
#endif

#include <csignal>
#include <iostream>
#include <thread>
#include <vector>

#ifndef NDEBUG
/**
 * @brief A signal handler for SIGILL.
 *
 * Forward SIGILL from illegal instructions to catchsegv in `ci.yaml`.
 * Provides additional debugging information via `-rdynamic`.
 */
static void sigill_handler(int sig) {
    assert(sig == SIGILL && "sigill_handler is for SIGILL");
    std::raise(SIGSEGV);
}

/**
 * @brief A signal handler for SIGABRT.
 *
 * Forward SIGABRT from `std::abort` and `assert` to catchsegv in `ci.yaml`.
 * Provides additional debugging information via `-rdynamic`.
 */
static void sigabrt_handler(int sig) {
    assert(sig == SIGABRT && "sigabrt_handler is for SIGABRT");
    std::raise(SIGSEGV);
}
#endif /// NDEBUG

/**
 * @brief A signal handler for SIGINT.
 *
 * Stops the execution of the application via the ILLIXR runtime.
 */
static void sigint_handler([[maybe_unused]] int sig) {
    assert(sig == SIGINT && "sigint_handler is for SIGINT");
    if (runtime_) {
        runtime_->stop();
    }
}

using namespace ILLIXR;

#ifdef ILLIXR_ANDROID_BUILD
extern "C" {
    // called from Java after permission is granted
    JNIEXPORT void JNICALL Java_com_example_ILLIXR_ILLIXRNativeActivity_nativeOnPermissionGranted(JNIEnv* env, jobject activity) {

    }
}

static void handle_cmd(struct android_app* app, int32_t cmd) {
    if (cmd == APP_CMD_INIT_WINDOW) {
//            std::vector<std::string> arguments = { "", "libandroid_imu_cam.so"};
//            std::vector<std::string> arguments = { "", "libslam.so" ,"librk4_integrator.so",
//                                                   "libpose_prediction.so",  "libcommon_lock.so", "libtimewarp_gl.so", "libgldemo.so"};
//            std::vector<std::string> arguments = { "",
//                                                   "libpose_prediction.so",  "libcommon_lock.so", "libtimewarp_gl.so", "libgldemo.so"};
        const std::vector<std::string> plugins = { "openvins", "offline_cam", "offline_imu" ,"rk4_integrator",
                                             "pose_prediction",  "common_lock", "timewarp_gl", "gldemo"};

        //EuRoC
        setenv("ILLIXR_DATA", "/sdcard/Android/data/com.example.native_activity/mav0", true);
        setenv("ILLIXR_LOG", "/sdcard/Android/data/com.example.native_activity/log.txt", true);

            //Android
//            setenv("ILLIXR_DATA", "/sdcard/Android/data/com.example.native_activity/android_new", true);
        setenv("ILLIXR_DEMO_DATA", "/sdcard/Android/data/com.example.native_activity/demo_data", true);
        setenv("ILLIXR_OFFLOAD_ENABLE", "False", true);
        setenv("ILLIXR_ALIGNMENT_ENABLE", "False", true);
        setenv("ILLIXR_ENABLE_VERBOSE_ERRORS", "False", true);
        setenv("ILLIXR_RUN_DURATION", "1000000", true);
        setenv("ILLIXR_ENABLE_PRE_SLEEP", "False", true);
        setenv("ILLIXR_ENABLE_PRE_SLEEP", "False", true);
        setenv("ILLIXR_TCP_CLIENT_IP", "141.142.60.47", true);
        setenv("ILLIXR_TCP_CLIENT_PORT", "9000", true);
        setenv("ILLIXR_TCP_SERVER_IP", "141.142.60.195", true);
        setenv("ILLIXR_TCP_SERVER_PORT", "9001", true);
        setenv("ILLIXR_IS_CLIENT", "1", true);

#else
int main(int argc, const char* argv[]) {
    cxxopts::Options options("ILLIXR", "Main program");
    options.show_positional_help();
    options.allow_unrecognised_options();
    // std::string illixr_data, illixr_demo_data, realsense_cam;
    // illixr_data = illixr_demo_data = realsense_cam = "";
    // bool offload_enable, alignment_enable, enable_verbose_errors, enable_pre_sleep;
    // offload_enable = alignment_enable = enable_verbose_errors = enable_pre_sleep = false;
    // long run_dur = 0;
    options.add_options()("d,duration", "The duration to run for", cxxopts::value<long>())(
        "data", "The data", cxxopts::value<std::string>())("demo_data", "The demo data", cxxopts::value<std::string>())(
        "enable_offload", "")("enable_alignment", "")("enable_verbose_errors", "")("enable_pre_sleep", "")(
        "h,help", "Produce help message")("realsense_cam", "", cxxopts::value<std::string>()->default_value("auto"))(
        "p,plugins", "The plugins to use",
        cxxopts::value<std::vector<std::string>>())("y,yaml", "Yaml config file", cxxopts::value<std::string>())("openxr", "");
    auto result = options.parse(argc, argv);
    if (result.count("help")) {
        std::cout << options.help() << std::endl;
        return EXIT_SUCCESS;
    }
#endif

#ifndef NDEBUG
    /// When debugging, register the SIGILL and SIGABRT handlers for capturing more info
    std::signal(SIGILL, sigill_handler);
    std::signal(SIGABRT, sigabrt_handler);
#endif /// NDEBUG

    /// Shutting down method 1: Ctrl+C
    std::signal(SIGINT, sigint_handler);
#ifdef ILLIXR_ANDROID_BUILD
        std::thread runtime_thread(ILLIXR::run, plugins, app->window);
        runtime_thread.join();
    }
#else
    return ILLIXR::run(result);
#endif
}

#ifdef ILLIXR_ANDROID_BUILD
void android_main(struct android_app* state) {
    state->onAppCmd = handle_cmd;
    while(true) {
        int ident;
        int events;
        struct android_poll_source* source;

        // If not animating, we will block forever waiting for events.
        // If animating, we loop until all events are read, then continue
        // to draw the next frame of animation.
        do {
            ident = ALooper_pollOnce(0, nullptr, &events,
                                     (void**)&source);
            if (ident >= 0) {
                // Process this event.
                if (source != nullptr) {
                    source->process(state, source);
                }
            }
        } while (ident >= 0);
    }
}
#endif // ILLIXR_ANDROID_BUILD
#endif // defined(ILLIXR_ANDROID_BUILD) && defined(ENABLE_MONADO)
