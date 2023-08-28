#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <unistd.h>

#include "illixr.hpp"
#include "illixr/error_util.hpp"
#include <yaml-cpp/yaml.h>

ILLIXR::runtime *r = nullptr;

using namespace ILLIXR;

int ILLIXR::run(const cxxopts::ParseResult &options) {
    std::chrono::seconds run_duration;
    std::vector<std::string> plugins;

#ifdef ILLIXR_MONADO_MAINLINE
    r = ILLIXR::runtime_factory();
#else
    r = ILLIXR::runtime_factory(nullptr);
#endif /// ILLIXR_MONADO_MAINLINE

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
    if (options.count("yaml")) {
        config = YAML::LoadFile(options["yaml"].as<std::string>());
    }
    if (options.count("duration")) {
        run_duration = std::chrono::seconds{options["duration"].as<long>()};
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
    bool have_group = false;
    if (options.count("group") || config["group"]) {
        std::string group;
        if (options.count("group")) {
            group = options["group"].as<std::string>();
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
        if (options.count("plugins")) {
            plugins = options["plugins"].as<std::vector<std::string>>();
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
