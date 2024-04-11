#include "illixr.hpp"
#include "illixr/error_util.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <unistd.h>
#include <yaml-cpp/yaml.h>

ILLIXR::runtime* r = nullptr;

using namespace ILLIXR;

int ILLIXR::run(const cxxopts::ParseResult& options) {
    std::chrono::seconds     run_duration;
    std::vector<std::string> plugins;

    r = ILLIXR::runtime_factory();

#ifndef NDEBUG
    /// Activate sleeping at application start for attaching gdb. Disables 'catchsegv'.
    /// Enable using the ILLIXR_ENABLE_PRE_SLEEP environment variable (see 'runner/runner/main.py:load_tests')
    const bool enable_pre_sleep = ILLIXR::str_to_bool(getenv_or("ILLIXR_ENABLE_PRE_SLEEP", "False"));
    if (enable_pre_sleep) {
        const pid_t pid = getpid();
        spdlog::get("illixr")->info("[main] Pre-sleep enabled.");
        spdlog::get("illixr")->info("[main] PID: {}", pid);
        spdlog::get("illixr")->info("[main] Sleeping for {} seconds...", ILLIXR_PRE_SLEEP_DURATION);
        sleep(ILLIXR_PRE_SLEEP_DURATION);
        spdlog::get("illixr")->info("[main] Resuming...");
    }
#endif /// NDEBUG
    // read in yaml config file
    YAML::Node config;
    if (options.count("yaml")) {
        std::cout << "Reading " << options["yaml"].as<std::string>() << std::endl;
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
    bool have_plugins = false;
    // run entry supersedes plugins entry
    for (auto item : {"plugins", "run"}) {
        if (options.count(item)) {
            plugins      = options[item].as<std::vector<std::string>>();
            have_plugins = true;
        } else if (config[item]) {
            std::stringstream tss(config[item].as<std::string>());
            while (tss.good()) {
                std::string substr;
                getline(tss, substr, ',');
                plugins.push_back(substr);
            }
            have_plugins = true;
        }
    }

    if (!have_plugins) {
        std::cout << "No plugins specified." << std::endl;
        std::cout << "A list of plugins must be given on the command line or in a YAML file" << std::endl;
        return EXIT_FAILURE;
    }
    std::vector<std::string> visualizers;
    if (options.count("vis")) {
        visualizers = options["vis"].as<std::vector<std::string>>();
    } else if (config["visualizers"]) {
        std::stringstream vss(config["visualizers"].as<std::string>());
        while (vss.good()) {
            std::string substr;
            getline(vss, substr, ',');
            visualizers.push_back(substr);
        }
    }
    if (!visualizers.empty())
        plugins.push_back(visualizers[0]);

    if (config["install_prefix"]) {
        std::string temp_path(getenv("LD_LIBRARY_PATH"));
        temp_path = config["install_prefix"].as<std::string>() + ":" + temp_path;
        setenv("LD_LIBRARY_PATH", temp_path.c_str(), true);
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
