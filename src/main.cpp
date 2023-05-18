#include "cxxopts.hpp"
#include "illixr/global_module_defs.hpp"
#include "runtime_impl.hpp"
#include "yaml-cpp/yaml.h"

#include <csignal>

#define GET_STRING(NAME, ENV)                                        \
    if (result.count(#NAME)) {                                       \
        setenv(#ENV, result[#NAME].as<std::string>().c_str(), true); \
    } else if (config[#NAME]) {                                      \
        setenv(#ENV, config[#NAME].as<std::string>().c_str(), true); \
    }

#define GET_BOOL(NAME, ENV)                     \
    if (result.count(#NAME) || config[#NAME]) { \
        bool val;                               \
        if (result.count(#NAME)) {              \
            val = result[#NAME].as<bool>();     \
        } else {                                \
            val = config[#NAME].as<bool>();     \
        }                                       \
        if (val) {                              \
            setenv(#ENV, "True", true);         \
        } else {                                \
            setenv(#ENV, "False", false);       \
        }                                       \
    }
#define _STR(y)      #y
#define STRINGIZE(x) _STR(x)
#define GET_LONG(NAME, ENV)                                                   \
    if (result.count(#NAME)) {                                                \
        setenv(#ENV, std::to_string(result[#NAME].as<long>()).c_str(), true); \
    } else if (config[#NAME]) {                                               \
        setenv(#ENV, std::to_string(config[#NAME].as<long>()).c_str(), true); \
    }

constexpr std::chrono::seconds          ILLIXR_RUN_DURATION_DEFAULT{60};
[[maybe_unused]] constexpr unsigned int ILLIXR_PRE_SLEEP_DURATION{10};

const std::vector<std::string> core_plugins = {"audio_pipeline", "timewarp_gl"},
                               rt_plugins = {"gtsam_integrator", "kimera_vio", "offline_imu", "offline_cam", "pose_prediction"},
                               monado_plugins = core_plugins + rt_plugins + std::vector<std::string>{"monado"},
                               native_plugins = core_plugins + rt_plugins +
    std::vector<std::string>{"ground_truth_slam", "gldemo", "debugview", "offload_data"},
                               ci_plugins = core_plugins + rt_plugins + std::vector<std::string>{"ground_truth_slam", "gldemo"},
                               all_plugins = core_plugins + rt_plugins +
    std::vector<std::string>{"monado", "ground_truth_slam", "gldemo", "debugview", "offload_data"};

ILLIXR::runtime* r;

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
    if (r) {
        r->stop();
    }
}

class cancellable_sleep {
public:
    template<typename T, typename R>
    bool sleep(std::chrono::duration<T, R> duration) {
        auto wake_up_time = std::chrono::system_clock::now() + duration;
        while (!_m_terminate.load() && std::chrono::system_clock::now() < wake_up_time) {
            std::this_thread::sleep_for(std::chrono::milliseconds{100});
        }
        return _m_terminate.load();
    }

    void cancel() {
        _m_terminate.store(true);
    }

private:
    std::atomic<bool> _m_terminate{false};
};

int main(int argc, const char* argv[]) {
    cxxopts::Options options("ILLIXR", "Main program");
    options.show_positional_help();
    std::chrono::seconds run_duration;
    // std::string illixr_data, illixr_demo_data, realsense_cam;
    // illixr_data = illixr_demo_data = realsense_cam = "";
    // bool offload_enable, alignment_enable, enable_verbose_errors, enable_pre_sleep;
    // offload_enable = alignment_enable = enable_verbose_errors = enable_pre_sleep = false;
    // long run_dur = 0;
    std::vector<std::string> plugins;
    options.add_options()("d,duration", "The duration to run for", cxxopts::value<long>())(
        "data", "The data", cxxopts::value<std::string>())("demo_data", "The demo data", cxxopts::value<std::string>())(
        "enable_offload", "")("enable_alignment", "")("enable_verbose_errors", "")("enable_pre_sleep", "")(
        "h,help", "Produce help message")("realsense_cam", "", cxxopts::value<std::string>()->default_value("auto"))(
        "p,plugins", "The plugins to use", cxxopts::value<std::vector<std::string>>())(
        "g,group", "The group of plugins to use: monado, native, ci, all",
        cxxopts::value<std::string>())("y,yaml", "Yaml config file", cxxopts::value<std::string>());
    auto result = options.parse(argc, argv);
    if (result.count("help")) {
        std::cout << options.help() << std::endl;
        return EXIT_SUCCESS;
    }
#ifdef ILLIXR_MONADO_MAINLINE
    r = ILLIXR::runtime_factory();
#else
    r = ILLIXR::runtime_factory(nullptr);
#endif /// ILLIXR_MONADO_MAINLINE

#ifndef NDEBUG
    /// When debugging, register the SIGILL and SIGABRT handlers for capturing more info
    std::signal(SIGILL, sigill_handler);
    std::signal(SIGABRT, sigabrt_handler);
#endif /// NDEBUG

    /// Shutting down method 1: Ctrl+C
    std::signal(SIGINT, sigint_handler);

#ifndef NDEBUG
    /// Activate sleeping at application start for attaching gdb. Disables 'catchsegv'.
    /// Enable using the ILLIXR_ENABLE_PRE_SLEEP environment variable (see 'runner/runner/main.py:load_tests')
    const bool enable_pre_sleep = ILLIXR::str_to_bool(getenv_or("ILLIXR_ENABLE_PRE_SLEEP", "False"));
    if (enable_pre_sleep) {
        const pid_t pid = getpid();
        std::cout << "[main] Pre-sleep enabled." << std::endl
                  << "[main] PID: " << pid << std::endl
                  << "[main] Sleeping for " << ILLIXR_PRE_SLEEP_DURATION << " seconds ..." << std::endl;
        sleep(ILLIXR_PRE_SLEEP_DURATION);
        std::cout << "[main] Resuming ..." << std::endl;
    }
#endif /// NDEBUG
    // read in yaml config file
    YAML::Node config;
    if (result.count("yaml")) {
        config = YAML::LoadFile(result["yaml"].as<std::string>());
    }
    if (result.count("duration")) {
        run_duration = std::chrono::seconds{result["duration"].as<long>()};
    } else if (config["duration"]) {
        run_duration = std::chrono::seconds{config["duration"].as<long>()};
    } else {
        run_duration = getenv("ILLIXR_RUN_DURATION")
            ? std::chrono::seconds{std::stol(std::string{getenv("ILLIXR_RUN_DURATION")})}
            : ILLIXR_RUN_DURATION_DEFAULT;
    }
    GET_STRING(data, ILLIXR_DATA)
    GET_STRING(demo_data, ILLIXR_DEMO_DATA)
    GET_BOOL(enable_offload, ILLIXR_OFFLOAD_ENABLE)
    GET_BOOL(alignment_enable, ILLIXR_ALIGNMENT_ENABLE)
    GET_BOOL(enable_verbose_errors, ILLIXR_ENABLE_VERBOSE_ERRORS)
    GET_BOOL(enable_pre_sleep, ILLIXR_ENABLE_PRE_SLEEP)
    GET_STRING(realsense_cam, REALSENSE_CAM)

    setenv("__GL_MaxFramesAllowed", "1", false);
    setenv("__GL_SYNC_TO_VBLANK", "1", false);
    bool have_group;
    if (result.count("group") || config["group"]) {
        std::string group;
        if (result.count("group")) {
            group = result["group"].as<std::string>();
        } else {
            group = config["group"].as<std::string>();
        }
        if (group == "monado" || group == "MONADO") {
            plugins    = monado_plugins;
            have_group = true;
        } else if (group == "native" || group == "NATIVE") {
            plugins    = native_plugins;
            have_group = true;
        } else if (group == "ci" || group == "CI") {
            plugins    = ci_plugins;
            have_group = true;
        } else if (group == "all" || group == "ALL") {
            plugins    = all_plugins;
            have_group = true;
        } else {
            have_group = false;
        }
    }
    if (!have_group) {
        if (result.count("plugins")) {
            plugins = result["plugins"].as<std::vector<std::string>>();
        } else if (config["plugins"]) {
            plugins = config["plugins"].as<std::vector<std::string>>();
        } else {
            std::cout << "No plugins specified." << std::endl;
            std::cout << "Either a list of plugins or a group name must be given. Groups are monado, native, ci, all, or none"
                      << std::endl;
            return EXIT_FAILURE;
        }
    }

    RAC_ERRNO_MSG("main after creating runtime");

    std::vector<std::string> lib_paths;
    std::transform(plugins.begin(), plugins.end(), std::back_inserter(lib_paths), [](const std::string& arg) {
        return "libplugin." + arg + STRINGIZE(ILLIXR_BUILD_SUFFIX) + ".so";
    });

    RAC_ERRNO_MSG("main before loading dynamic libraries");
    r->load_so(lib_paths);

    cancellable_sleep cs;
    std::thread       th{[&] {
        cs.sleep(run_duration);
        r->stop();
    }};

    r->wait(); // blocks until shutdown is r->stop()

    // cancel our sleep, so we can join the other thread
    cs.cancel();
    th.join();

    delete r;
    return 0;
}
