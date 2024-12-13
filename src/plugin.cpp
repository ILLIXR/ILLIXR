#include "illixr.hpp"
#include "illixr/error_util.hpp"
#include "illixr/switchboard.hpp"

#ifndef BOOST_DATE_TIME_NO_LIB
    #define BOOST_DATE_TIME_NO_LIB
#endif
#include <algorithm>
#ifdef OXR_INTERFACE
    #include <boost/interprocess/mapped_region.hpp>
    #include <boost/interprocess/shared_memory_object.hpp>
#endif
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <unistd.h>
#include <yaml-cpp/yaml.h>

ILLIXR::runtime* r = nullptr;

using namespace ILLIXR;
#ifdef OXR_INTERFACE
namespace b_intp            = boost::interprocess;
const char* illixr_shm_name = "ILLIXR_OXR_SHM";
#endif

int ILLIXR::run(const cxxopts::ParseResult& options) {
#ifdef OXR_INTERFACE
    b_intp::shared_memory_object shm_obj;
#endif
    try {
        std::chrono::seconds     run_duration;
        std::vector<std::string> plugins;

        r = ILLIXR::runtime_factory();
        // set internal env_vars
        const std::shared_ptr<switchboard> sb = r->get_switchboard();

        YAML::Node config;
        if (options.count("yaml")) {
            std::cout << "Reading " << options["yaml"].as<std::string>() << std::endl;
            config = YAML::LoadFile(options["yaml"].as<std::string>());
        }

        // read in config file first, as command line args will override
        for (auto& item : sb->env_names()) {
            if (config[item])
                sb->set_env(item, config[item].as<std::string>());
        }
        // command line specified env_vars
        for (auto& item : options.unmatched()) {
            bool                                   matched = false;
            cxxopts::values::parser_tool::ArguDesc ad      = cxxopts::values::parser_tool::ParseArgument(item.c_str(), matched);

            if (!sb->get_env(ad.arg_name, "").empty()) {
                if (!ad.set_value)
                    ad.value = "True";
                sb->set_env(ad.arg_name, ad.value);
                setenv(ad.arg_name.c_str(), ad.value.c_str(), 1); // env vars from command line take precedence
            }
        }

#ifndef NDEBUG
        /// Activate sleeping at application start for attaching gdb. Disables 'catchsegv'.
        /// Enable using the ILLIXR_ENABLE_PRE_SLEEP environment variable (see 'runner/runner/main.py:load_tests')
        const bool enable_pre_sleep = sb->get_env_bool("ILLIXR_ENABLE_PRE_SLEEP", "False");
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
        if (options.count("duration")) {
            run_duration = std::chrono::seconds{options["duration"].as<long>()};
        } else if (config["duration"]) {
            run_duration = std::chrono::seconds{config["duration"].as<long>()};
        } else {
            run_duration = (!sb->get_env("ILLIXR_RUN_DURATION").empty())
                ? std::chrono::seconds{std::stol(std::string{sb->get_env("ILLIXR_RUN_DURATION")})}
                : ILLIXR_RUN_DURATION_DEFAULT;
        }
        GET_STRING(data, ILLIXR_DATA)
        GET_STRING(demo_data, ILLIXR_DEMO_DATA)
        GET_BOOL(enable_offload, ILLIXR_OFFLOAD_ENABLE)
        GET_BOOL(alignment_enable, ILLIXR_ALIGNMENT_ENABLE)
        GET_BOOL(enable_verbose_errors, ILLIXR_ENABLE_VERBOSE_ERRORS)
        GET_BOOL(enable_pre_sleep, ILLIXR_ENABLE_PRE_SLEEP)
        GET_BOOL(openxr, ILLIXR_OPENXR)
        GET_STRING(realsense_cam, REALSENSE_CAM)

#ifdef OXR_INTERFACE
        shm_obj.remove(illixr_shm_name);
        shm_obj = b_intp::shared_memory_object(b_intp::create_only, illixr_shm_name, b_intp::read_write);
        shm_obj.truncate(64);
        b_intp::mapped_region region(shm_obj, b_intp::read_write);
        auto                  shp = reinterpret_cast<std::uintptr_t>(sb.get());
        std::memcpy(region.get_address(), (void*) shp, sizeof(shp));
#endif

        setenv("__GL_MaxFramesAllowed", "1", false);
        setenv("__GL_SYNC_TO_VBLANK", "1", false);
        bool have_plugins = false;
        // run entry supersedes plugins entry
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

        if (config["install_prefix"]) {
            std::string temp_path(sb->get_env("LD_LIBRARY_PATH"));
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
    } catch (...) {
#ifdef OXR_INTERFACE
        shm_obj.remove(illixr_shm_name);
#endif
    }
    return 0;
}
