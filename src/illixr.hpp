#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <atomic>

#include "cxxopts.hpp"
#include "illixr/runtime.hpp"

#define GET_STRING(NAME, ENV)                                        \
    if (options.count(#NAME)) {                                       \
        setenv(#ENV, options[#NAME].as<std::string>().c_str(), true); \
    } else if (config[#NAME]) {                                      \
        setenv(#ENV, config[#NAME].as<std::string>().c_str(), true); \
    }

#define GET_BOOL(NAME, ENV)                     \
    if (options.count(#NAME) || config[#NAME]) { \
        bool val;                               \
        if (options.count(#NAME)) {              \
            val = options[#NAME].as<bool>();     \
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
    if (options.count(#NAME)) {                                                \
        setenv(#ENV, std::to_string(options[#NAME].as<long>()).c_str(), true); \
    } else if (config[#NAME]) {                                               \
        setenv(#ENV, std::to_string(config[#NAME].as<long>()).c_str(), true); \
    }

constexpr std::chrono::seconds          ILLIXR_RUN_DURATION_DEFAULT{60};
[[maybe_unused]] constexpr unsigned int ILLIXR_PRE_SLEEP_DURATION{10};
template<typename T>
std::vector<T> operator+(const std::vector<T>& a, const std::vector<T>& b) {
    std::vector<T> c = a;
    c.insert(c.end(), b.begin(), b.end());
    return c;
}

const std::vector<std::string> core_plugins = {"audio_pipeline", "timewarp_gl"},
        rt_plugins = {"gtsam_integrator", "kimera_vio", "offline_imu", "offline_cam", "pose_prediction"},
        monado_plugins = core_plugins + rt_plugins + std::vector<std::string>{"monado"},
        native_plugins = core_plugins + rt_plugins +
                         std::vector<std::string>{"ground_truth_slam", "gldemo", "debugview", "offload_data"},
        ci_plugins = core_plugins + rt_plugins + std::vector<std::string>{"ground_truth_slam", "gldemo"},
        all_plugins = core_plugins + rt_plugins +
                      std::vector<std::string>{"monado", "ground_truth_slam", "gldemo", "debugview", "offload_data"};

extern ILLIXR::runtime *r;
namespace ILLIXR {
    int run(const cxxopts::ParseResult &options);

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
}