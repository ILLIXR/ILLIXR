#pragma once

#ifndef ILLIXR_ANDROID_BUILD
#include "cxxopts.hpp"
#endif
#include "illixr/runtime.hpp"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#ifdef ILLIXR_ANDROID_BUILD
#include <android/native_window_jni.h>
#else
#define GET_STRING(NAME, ENV)                                                     \
    if (options.count(#NAME)) {                                                   \
        switchboard_->set_env(#ENV, options[#NAME].as<std::string>());            \
    } else if (config["env_vars"][#NAME]) {                                       \
        switchboard_->set_env(#ENV, config["env_vars"][#NAME].as<std::string>()); \
    }

#define GET_BOOL(NAME, ENV)                             \
    if (options.count(#NAME) || config[#NAME]) {        \
        bool val;                                       \
        if (options.count(#NAME)) {                     \
            val = options[#NAME].as<bool>();            \
        } else {                                        \
            val = config["env_vars"][#NAME].as<bool>(); \
        }                                               \
        if (val) {                                      \
            switchboard_->set_env(#ENV, "True");        \
        } else {                                        \
            switchboard_->set_env(#ENV, "False");       \
        }                                               \
    }
#define _STR(y)      #y
#define STRINGIZE(x) _STR(x)
#endif

constexpr std::chrono::seconds          ILLIXR_RUN_DURATION_DEFAULT{60};
[[maybe_unused]] constexpr unsigned int ILLIXR_PRE_SLEEP_DURATION{10};

#ifndef ILLIXR_ANDROID_BUILD
template<typename T>
std::vector<T> operator+(const std::vector<T>& a, const std::vector<T>& b) {
    std::vector<T> c = a;
    c.insert(c.end(), b.begin(), b.end());
    return c;
}
#endif

extern ILLIXR::runtime* runtime_;

namespace ILLIXR {
#ifdef ILLIXR_ANDROID_BUILD
int run(const std::vector<std::string>& plugins, ANativeWindow *window);
#else
int run(const cxxopts::ParseResult& options);
#endif

class cancellable_sleep {
public:
    template<typename T, typename R>
    bool sleep(std::chrono::duration<T, R> duration) {
        auto wake_up_time = std::chrono::system_clock::now() + duration;
        while (!terminate_.load() && std::chrono::system_clock::now() < wake_up_time) {
            std::this_thread::sleep_for(std::chrono::milliseconds{100});
        }
        return terminate_.load();
    }

    void cancel() {
        terminate_.store(true);
    }

private:
    std::atomic<bool> terminate_{false};
};
} // namespace ILLIXR
