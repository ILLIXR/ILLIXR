#pragma once

#include "cxxopts.hpp"
#include "illixr/runtime.hpp"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#define GET_STRING(NAME, ENV)                                         \
    if (options.count(#NAME)) {                                       \
        setenv(#ENV, options[#NAME].as<std::string>().c_str(), true); \
    } else if (config[#NAME]) {                                       \
        setenv(#ENV, config[#NAME].as<std::string>().c_str(), true);  \
    }

#define GET_BOOL(NAME, ENV)                      \
    if (options.count(#NAME) || config[#NAME]) { \
        bool val;                                \
        if (options.count(#NAME)) {              \
            val = options[#NAME].as<bool>();     \
        } else {                                 \
            val = config[#NAME].as<bool>();      \
        }                                        \
        if (val) {                               \
            setenv(#ENV, "True", true);          \
        } else {                                 \
            setenv(#ENV, "False", false);        \
        }                                        \
    }
#define STR(y)       #y
#define STRINGIZE(x) STR(x)

constexpr std::chrono::seconds          ILLIXR_RUN_DURATION_DEFAULT{60};
[[maybe_unused]] constexpr unsigned int ILLIXR_PRE_SLEEP_DURATION{10};

template<typename T>
std::vector<T> operator+(const std::vector<T>& a, const std::vector<T>& b) {
    std::vector<T> c = a;
    c.insert(c.end(), b.begin(), b.end());
    return c;
}

extern ILLIXR::runtime* runtime_;

namespace ILLIXR {
int run(const cxxopts::ParseResult& options);

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
