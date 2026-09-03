#ifndef ENABLE_MONADO

#    include "illixr.hpp"
#    ifdef __ANDROID__
#        include "android/profile_picker_dialog.hpp"

#        include <EGL/egl.h>
#        include <thread>
#    else
#        include <iostream>
#    endif
#    include <csignal>

#    ifndef NDEBUG
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
#    endif /// NDEBUG

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

#    ifdef __ANDROID__
extern "C" {
// called from Java after permission is granted
JNIEXPORT void JNICALL Java_com_example_ILLIXR_ILLIXRNativeActivity_nativeOnPermissionGranted(JNIEnv* env, jobject activity) { }
}

/// Holds the ILLIXR runtime thread so it can be joined from android_main() once shutdown is
/// requested, instead of being joined synchronously inside handle_cmd(). Joining here would
/// block the android_app looper from processing any further lifecycle commands.
static std::thread runtime_thread_;

static void handle_cmd(struct android_app* app, int32_t cmd) {
#    else
int main(int argc, const char* argv[]) {
#    endif

#    ifdef __ANDROID__
    if (cmd == APP_CMD_INIT_WINDOW && !runtime_thread_.joinable()) {
        // EuRoC
        setenv("ILLIXR_DATA", "/sdcard/Android/data/com.example.native_activity/mav0", true);
        setenv("ILLIXR_LOG", "/sdcard/Android/data/com.example.native_activity/log.txt", true);

        setenv("ILLIXR_DEMO_DATA", "/sdcard/Android/data/com.example.native_activity/demo_data", true);
        setenv("ILLIXR_RUN_DURATION", "1000000", true);
        setenv("ILLIXR_IS_CLIENT", "1", true);
        setenv("ILLIXR_USE_DEPTH_IMAGES", "0", true);
        setenv("ILLIXR_USE_MOTION_VECTOR_IMAGES", "0", true);
        // Show the profile picker dialog.  It extracts bundled profile YAML
        // files from the APK assets to the app's internal storage, presents
        // them in a spinner, and returns the full path to the chosen file.
        // If the user cancels, or no profiles are found, the app exits.
        const std::string yaml_path = ILLIXR::show_profile_picker_dialog(app);
        if (yaml_path.empty()) {
            // User cancelled or no profiles available — cannot continue.

            return;
        }
#    else
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
#    endif
#    ifndef NDEBUG
        /// When debugging, register the SIGILL and SIGABRT handlers for capturing more info
        std::signal(SIGILL, sigill_handler);
        std::signal(SIGABRT, sigabrt_handler);
#    endif /// NDEBUG

        /// Shutting down method 1: Ctrl+C
        std::signal(SIGINT, sigint_handler);
#    ifdef __ANDROID__
        /// Run the ILLIXR runtime on its own thread and return control to the caller immediately;
        /// it is joined later, from android_main(), once APP_CMD_DESTROY has been processed.
        runtime_thread_ = std::thread(ILLIXR::run, app, yaml_path);
    } else if (cmd == APP_CMD_DESTROY) {
        /// Shutting down method 2: the activity is being destroyed
        if (runtime_) {
            runtime_->stop();
        }
    }
#    else
    return ILLIXR::run(result);
#    endif
}

#    ifdef __ANDROID__
void android_main(struct android_app* state) {
    state->onAppCmd = handle_cmd;
    while (!state->destroyRequested) {
        int                         ident;
        int                         events;
        struct android_poll_source* source;

        // This loop performs no per-frame work of its own -- rendering happens on
        // runtime_thread_ via OpenXR/Vulkan -- so it blocks indefinitely and only wakes to
        // dispatch the next lifecycle command or input event.
        do {
            ident = ALooper_pollOnce(-1, nullptr, &events, (void**) &source);
            if (ident >= 0) {
                // Process this event.
                if (source != nullptr) {
                    source->process(state, source);
                }
            }
        } while (ident >= 0 && !state->destroyRequested);
    }

    /// APP_CMD_DESTROY has been processed and handle_cmd() has called runtime_->stop(); wait for
    /// the runtime thread to actually finish before returning, so android_app_destroy() can safely
    /// tear down the native state behind it.
    if (runtime_thread_.joinable()) {
        runtime_thread_.join();
    }
}
#    endif

#endif
