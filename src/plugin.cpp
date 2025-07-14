#include "illixr.hpp"
#include "illixr/error_util.hpp"
#include "illixr/switchboard.hpp"

#ifndef ILLIXR_INSTALL_PATH
    #error "ILLIXR_INSTALL_PATH must be defined"
#endif
#ifndef BOOST_DATE_TIME_NO_LIB
    #define BOOST_DATE_TIME_NO_LIB
#endif
#include <algorithm>
#include <boost/algorithm/string/join.hpp>
#include <cstdlib>
#include <iostream>
#include <pwd.h>
#include <sstream>
#include <stdexcept>
#include <unistd.h>
#include <yaml-cpp/yaml.h>

namespace ILLIXR {
struct Dependency {
    std::string                                     name;
    std::map<std::string, std::vector<std::string>> deps;

    bool operator==(const Dependency& rhs) const {
        return name == rhs.name;
    }

    bool operator==(const std::string& rhs) const {
        return name == rhs;
    }
};
} // namespace ILLIXR

namespace YAML {
template<>
struct convert<ILLIXR::Dependency> {
    static Node encode(const ILLIXR::Dependency& rhs) {
        Node node;
        node["plugin"] = rhs.name;
        for (const auto& [key, value] : rhs.deps) {
            Node dep_node;
            dep_node["needs"] = key;
            for (const auto& v : value) {
                dep_node["provided_by"].push_back(v);
            }
            node["dependencies"].push_back(dep_node);
        }
        return node;
    }

    static bool decode(const Node& node, ILLIXR::Dependency& rhs) {
        if (node.size() != 2) {
            return false;
        }
        rhs.name  = node["plugin"].as<std::string>();
        Node deps = node["dependencies"];
        if (!deps.IsSequence()) {
            return false;
        }
        for (const auto& nd : deps) {
            if (nd.size() != 2) {
                return false;
            }
            auto dep_name = nd["needs"].as<std::string>();
            auto prov     = nd["provided_by"].as<std::vector<std::string>>();

            rhs.deps[dep_name] = prov;
        }
        return true;
    }
};
} // namespace YAML

ILLIXR::runtime* runtime_ = nullptr;

using namespace ILLIXR;

/*std::string get_exec_path() {
    char        result[PATH_MAX];
    ssize_t     count = readlink("/proc/self/exe", result, PATH_MAX);
    std::string exe_dir(result, (count > 0) ? count : 0);
    return exe_dir.substr(0, exe_dir.find_last_of("/\\"));
}*/

std::string get_home_dir() {
    struct passwd* pw = getpwuid(getuid());
    return {pw->pw_dir};
}

void check_plugins(std::vector<std::string>& plugins, const std::vector<ILLIXR::Dependency>& dep_map) {
    std::vector<std::string> ordered_plugins;
    ordered_plugins.reserve(plugins.size());

    auto resolve = [&dep_map, &ordered_plugins, &plugins](std::vector<std::string>::iterator it, auto&& resolve) {
        auto find_it = std::find(dep_map.begin(), dep_map.end(), *it);
        // if the plugin does not have any dependencies, then just add it to the list
        if (find_it == dep_map.end()) {
            if (std::find(ordered_plugins.begin(), ordered_plugins.end(), *it) == ordered_plugins.end())
                ordered_plugins.push_back(*it);
            return false;
        }
        bool mod = false;
        // go through each dependency and see if it is specified
        for (const auto& [item, needs] : find_it->deps) {
            bool dep_found = false;
            // first check plugins which are already in the list, if found, then we are good.
            for (const auto& provided_by : needs) {
                if (std::find(ordered_plugins.begin(), ordered_plugins.end(), provided_by) != ordered_plugins.end()) {
                    dep_found = true;
                    break;
                }
            }
            // try finding it in the rest of the list
            if (!dep_found) {
                bool rdep_found = false;
                for (const auto& provided_by : needs) {
                    auto r_find_it = std::find(plugins.begin(), plugins.end(), provided_by);
                    if (r_find_it != plugins.end()) {
                        rdep_found = true;
                        mod        = true;
                        resolve(r_find_it, resolve);
                        if (std::find(ordered_plugins.begin(), ordered_plugins.end(), provided_by) == ordered_plugins.end())
                            ordered_plugins.push_back(provided_by);
                        break;
                    }
                }
                if (!rdep_found) {
                    spdlog::get("illixr")->warn(
                        "Potential missing plugin dependency. Plugin " + *it + " requests a provider of " + item +
                        " to be included in the plugin list. This can be provided by one of the following plugins: " +
                        boost::algorithm::join(needs, ", "));
                }
            }
        }
        return mod;
    };

    bool modified = false;
    for (auto iter = plugins.begin(); iter != plugins.end(); iter++) {
        std::string input = *iter;
        modified |= resolve(iter, resolve);

        if (std::find(ordered_plugins.begin(), ordered_plugins.end(), *iter) == ordered_plugins.end())
            ordered_plugins.push_back(*iter);
    }
    if (modified)
        plugins = ordered_plugins;
}

int ILLIXR::run(const cxxopts::ParseResult& options) {
    std::chrono::seconds     run_duration;
    std::vector<std::string> plugins;
    try {
        runtime_ = ILLIXR::runtime_factory();
        // set internal env_vars
        // const std::shared_ptr<switchboard> sb = r->get_switchboard();

        // set internal env_vars
        std::shared_ptr<switchboard> switchboard_ = runtime_->get_switchboard();

        // read in yaml config file
        YAML::Node config;
        // std::string exec_path = get_exec_path();
        // setenv("ILLIXR_BINARY_PATH", exec_path.c_str(), 1);
        setenv("ILLIXR_BINARY_PATH", ILLIXR_INSTALL_PATH, 1);
        std::string home_dir = get_home_dir();
        if (options.count("yaml")) {
            std::cout << "Reading " << options["yaml"].as<std::string>() << std::endl;
            auto                     config_file_full = options["yaml"].as<std::string>();
            std::string              config_file      = config_file_full.substr(config_file_full.find_last_of("/\\") + 1);
            std::vector<std::string> config_list      = {config_file,
                                                         config_file_full,
                                                         home_dir + "/.illixr/profiles/" + config_file_full,
                                                         home_dir + "/.illixr/profiles/" + config_file,
                                                         home_dir + "/" + config_file_full,
                                                         home_dir + "/" + config_file,
                                                         std::string(ILLIXR_INSTALL_PATH) + "/share/illixr/profiles/" +
                                                             config_file_full,
                                                         std::string(ILLIXR_INSTALL_PATH) + "/share/illixr/profiles/" + config_file};
            for (auto& filepath : config_list) {
                try {
                    config = YAML::LoadFile(filepath);
                    break;
                } catch (YAML::BadFile&) { }
            }
            if (config.size() == 0) {
                spdlog::get("illixr")->error("Could not load given config file: " + config_file_full);
                throw std::runtime_error("Could not load given config file: " + config_file_full);
            }
            config_list.clear();
        }

        // set env vars from config file first, as command line args will override
        for (const auto& item : config["env_vars"]) {
            const auto val = item.first.as<std::string>();
            if (std::find(ignore_vars.begin(), ignore_vars.end(), val) == ignore_vars.end())
                switchboard_->set_env(val, item.second.as<std::string>());
        }
        // command line specified env_vars
        for (auto& item : options.unmatched()) {
            bool                                   matched = false;
            cxxopts::values::parser_tool::ArguDesc ad      = cxxopts::values::parser_tool::ParseArgument(item.c_str(), matched);

            if (switchboard_->get_env(ad.arg_name, "").empty()) {
                if (!ad.set_value)
                    ad.value = "True";
                switchboard_->set_env(ad.arg_name, ad.value);
                setenv(ad.arg_name.c_str(), ad.value.c_str(), 1); // env vars from command line take precedence
            }
        }

#ifndef NDEBUG
        /// Activate sleeping at application start for attaching gdb. Disables 'catchsegv'.
        /// Enable using the ILLIXR_ENABLE_PRE_SLEEP environment variable (see 'runner/runner/main.py:load_tests')
        const bool enable_pre_sleep = switchboard_->get_env_bool("ILLIXR_ENABLE_PRE_SLEEP", "False");
        if (enable_pre_sleep) {
            const pid_t pid = getpid();
            spdlog::get("illixr")->info("[main] Pre-sleep enabled.");
            spdlog::get("illixr")->info("[main] PID: {}", pid);
            spdlog::get("illixr")->info("[main] Sleeping for {} seconds...", ILLIXR_PRE_SLEEP_DURATION);
            sleep(ILLIXR_PRE_SLEEP_DURATION);
            spdlog::get("illixr")->info("[main] Resuming...");
        }
#endif /// NDEBUG

        if (options.count("duration")) {
            run_duration = std::chrono::seconds{options["duration"].as<long>()};
        } else if (config["env_vars"]["duration"]) {
            run_duration = std::chrono::seconds{config["env_vars"]["duration"].as<long>()};
        } else {
            run_duration = (!switchboard_->get_env("ILLIXR_RUN_DURATION").empty())
                ? std::chrono::seconds{std::stol(std::string{switchboard_->get_env("ILLIXR_RUN_DURATION")})}
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

        setenv("__GL_MaxFramesAllowed", "1", false);
        setenv("__GL_SYNC_TO_VBLANK", "1", false);

        std::vector<ILLIXR::Dependency> dep_map;
        std::vector<std::string>        dep_list   = {"plugin_deps.yaml", home_dir + "/.illixr/profiles/plugin_deps.yaml",
                                                      std::string(ILLIXR_INSTALL_PATH) + "/share/illixr/profiles/plugin_deps.yaml"

        };
        bool                            dep_loaded = false;
        for (auto& dep_file : dep_list) {
            try {
                YAML::Node plugin_deps = YAML::LoadFile(dep_file);
#ifndef NDEBUG
                spdlog::get("illixr")->info("Located plugin dependency map file (" + dep_file +
                                            "), verifying plugin dependencies.");
#endif
                dep_map.reserve(plugin_deps["dep_map"].size());
                for (const auto& node : plugin_deps["dep_map"])
                    dep_map.push_back(node.as<ILLIXR::Dependency>());
                dep_loaded = true;
                break;
            } catch (YAML::BadFile& bf) { }
        }

        if (!dep_loaded)
            spdlog::get("illixr")->info("Could not load plugin dependency map file, cannot verify plugin dependencies.");

        bool have_plugins = false;
        // run entry supersedes plugins entry
        for (auto item : {"plugins"}) {
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

        check_plugins(plugins, dep_map);
        if (config["install_prefix"]) {
            std::string temp_path(switchboard_->get_env("LD_LIBRARY_PATH"));
            temp_path = config["install_prefix"].as<std::string>() + ":" + temp_path;
            setenv("LD_LIBRARY_PATH", temp_path.c_str(), true);
        }

        // prevent double free
        switchboard_.reset();

        RAC_ERRNO_MSG("main after creating runtime");

        std::vector<std::string> lib_paths;
        std::transform(plugins.begin(), plugins.end(), std::back_inserter(lib_paths), [](const std::string& arg) {
            return "libplugin." + arg + STRINGIZE(ILLIXR_BUILD_SUFFIX) + ".so";
        });

        RAC_ERRNO_MSG("main before loading dynamic libraries");
        runtime_->load_so(lib_paths);

        cancellable_sleep cs;
        std::thread       th{[&] {
            cs.sleep(run_duration);
            runtime_->stop();
        }};

        runtime_->wait(); // blocks until shutdown is runtime_->stop()

        // cancel our sleep, so we can join the other thread
        cs.cancel();
        th.join();

        delete runtime_;
    } catch (const std::exception& ex) {
        std::cout << "ERROR: Exception caught in main: " << ex.what() << std::endl;
        delete runtime_;
    }
    return 0;
}
