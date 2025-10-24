#pragma once

//

#include "error_util.hpp"

#include <chrono>
#include <cstring>
#include <functional>
#include <iostream>
#include <spdlog/spdlog.h>
#include <string>
#include <thread>
#include <utility>

#if defined(_WIN32) || defined(_WIN64)
#include <Windows.h>
#include <time.h>
#include <atomic>
#include <cstdlib>
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTINIC 1
#endif
#ifndef CLOCK_THREAD_CPUTIME_ID
#define CLOCK_THREAD_CPUTIME_ID 3
#endif

typedef int clockid_t;

static int illixr_clock_gettime(clockid_t clock_id, timespec* ts) {
    if (clock_id == CLOCK_MONOTINIC) {
        static LARGE_INTEGER start_ticks;
        static LARGE_INTEGER frequency;
        static bool          initialized = false;

        if (!initialized) {
            QueryPerformanceFrequency(&frequency);
            QueryPerformanceCounter(&start_ticks);
            initialized = true;
        }

        LARGE_INTEGER current_ticks;
        QueryPerformanceCounter(&current_ticks);

        LONGLONG diff = current_ticks.QuadPart - start_ticks.QuadPart;
        ts->tv_sec    = diff / frequency.QuadPart;
        ts->tv_nsec   = static_cast<long>(((diff % frequency.QuadPart) * 1000000000) / frequency.QuadPart);
        return 0;
    } else if (clock_id == CLOCK_THREAD_CPUTIME_ID) {
        FILETIME creation_time, exit_time, kernel_time, user_time;
        if (!GetThreadTimes(GetCurrentThread(), &creation_time, &exit_time, &kernel_time, &user_time))
            return -1;

        ULARGE_INTEGER ktime, utime;
        ktime.LowPart = kernel_time.dwLowDateTime;
        ktime.HighPart = kernel_time.dwHighDateTime;

        utime.LowPart = user_time.dwLowDateTime;
        utime.HighPart = user_time.dwHighDateTime;

        uint64_t total_time_100ns = ktime.QuadPart + utime.QuadPart;

        // convert to ns
        uint64_t total_time_ns = total_time_100ns * 100;
        ts->tv_sec             = static_cast<time_t>(total_time_ns / 1000000000ULL);
        ts->tv_nsec            = static_cast<time_t>(total_time_ns % 1000000000ULL);
        return 0;
    }
    return -1;
}

#endif // _WIN32 || _WIN64

/**
 * @brief A C++ translation of [clock_gettime][1]
 *
 * [1]: https://linux.die.net/man/3/clock_gettime
 *
 */
static inline std::chrono::nanoseconds cpp_clock_get_time(clockid_t clock_id) {
    /* This ensures the compiler won't reorder this function call; Pretend like it has memory side effects. */
    timespec time_spec{};

    RAC_ERRNO_MSG("cpu_timer before clock_get_time");
#if defined(_WIN32) || defined(_WIN64)
    _ReadWriteBarrier();
    #else
    asm volatile(""
                 : /* OutputOperands */
                 : /* InputOperands */
                 : "memory" /* Clobbers */);
    #endif

    if (illixr_clock_gettime(clock_id, &time_spec)) {
        throw std::runtime_error{std::string{"clock_get_time returned "} + strerror(errno)};
    }
    RAC_ERRNO_MSG("cpu_timer after clock_get_time");

#if defined(_WIN32) || defined(_WIN64)
    _ReadWriteBarrier();
#else
    asm volatile(""
                 : /* OutputOperands */
                 : /* InputOperands */
                 : "memory" /* Clobbers */);
#endif
    return std::chrono::seconds{time_spec.tv_sec} + std::chrono::nanoseconds{time_spec.tv_nsec};
}

/**
 * @brief Gets the CPU time for the calling thread.
 */
static inline std::chrono::nanoseconds thread_cpu_time() {
    RAC_ERRNO_MSG("cpu_timer before cpp_clock_get_time");
    return cpp_clock_get_time(CLOCK_THREAD_CPUTIME_ID);
}

/**
 * @brief a timer that times until the end of the code block ([RAII]).
 *
 * See [[2][2]] for how code-blocks are defined in C++.
 *
 * `now` can be any type that takes no arguments and returns a
 * subtractable type.
 *
 * Example usage:
 *
 * \code{.cpp}
 * {
 *     // stuff that won't get timed.
 *     std::chrono::nanoseconds ns;
 *     timer<decltype((thread_cpu_time))> timer_obj {thread_cpu_time, ns};
 *     // stuff that gets timed.
 * }
 * // stuff that won't get timed.
 * std::cout << ns.count() << std::endl;
 * \endcode
 *
 * [1]: https://en.wikipedia.org/wiki/Resource_acquisition_is_initialization
 * [2]: https://www.geeksforgeeks.org/anonymous-classes-in-cpp/
 *
 */
template<typename Now_func, typename Time_point = decltype(std::declval<Now_func>()()),
         typename Duration = decltype(std::declval<Time_point>() - std::declval<Time_point>())>
class timer {
public:
    timer(const Now_func& now, Duration& duration)
        : now_{now}
        , duration_{duration} {
        start_ = now_();
    }

    ~timer() {
        duration_ = now_() - start_;
    }

private:
    const Now_func&            now_;
    [[maybe_unused]] Duration& duration_;
    Time_point                 start_;
};

template<typename Duration, typename Out = decltype(std::declval<Duration>().count())>
[[maybe_unused]] typename std::enable_if<std::is_integral<Out>::value, Out>::type count_duration(Duration t) {
    return std::chrono::duration_cast<std::chrono::nanoseconds, typename Duration::rep, typename Duration::period>(t).count();
}

template<typename Duration>
[[maybe_unused]] [[maybe_unused]] typename std::enable_if<std::is_integral<Duration>::value, Duration>::type
count_duration(Duration t) {
    return t;
}

/**
 * @brief Like timer, but prints the output.
 *
 * See PRINT_CPU_TIME_FOR_THIS_BLOCK(name)
 *
 */
template<typename Now_func, typename Time_point = decltype(std::declval<Now_func>()()),
         typename Duration = decltype(std::declval<Time_point>() - std::declval<Time_point>())>
class print_timer {
public:
    print_timer(const std::string& name, const Now_func& now)
        : print_in_destructor_{name, duration_}
        , timer_{now, duration_} { }

private:
    class print_in_destructor {
    public:
        [[maybe_unused]] print_in_destructor(std::string account_name, const Duration& duration)
            : account_name_{std::move(account_name)}
            , duration_{duration} { }

        ~print_in_destructor() {
            // std::ostringstream os;
            // os << "cpu_timer," << account_name_ << "," << count_duration<duration>(duration_) << "\n";
            if (rand() % 100 == 0) {
#ifndef NDEBUG
                spdlog::get("illixr")->info("cpu_timer.hpp is DEPRECATED. See logging.hpp.");
#endif
            }
        }

    private:
        const std::string                account_name_;
        [[maybe_unused]] const Duration& duration_;
    };

    // NOTE that the destructors get called in reverse order!
    // This is important, because timer_'s destructor records the timing information
    // Then, print_in_destructor_ prints it
    // Then, we can destroy duration_.
    Duration                                    duration_;
    const print_in_destructor                   print_in_destructor_;
    const timer<Now_func, Time_point, Duration> timer_;
};

static std::size_t gen_serial_no() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch())
        .count();
}

class should_profile_class {
public:
    should_profile_class() {
        const char* ILLIXR_STDOUT_METRICS = getenv("ILLIXR_STDOUT_METRICS"); // can't use switchboard interface here
        actually_should_profile_          = ILLIXR_STDOUT_METRICS && (strcmp(ILLIXR_STDOUT_METRICS, "y") == 0);
    }

    bool operator()() const {
        return actually_should_profile_;
    }

private:
    bool actually_should_profile_;
};

static should_profile_class should_profile;

class print_timer2 {
public:
    explicit print_timer2(std::string name)
        : name_{std::move(name)}
        , serial_no_{should_profile() ? gen_serial_no() : std::size_t{0}}
        , wall_time_start_{should_profile() ? std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                  std::chrono::high_resolution_clock::now().time_since_epoch())
                                                  .count()
                                            : std::size_t{0}}
        , cpu_time_start_{should_profile() ? thread_cpu_time().count() : std::size_t{0}} { }

    ~print_timer2() {
        if (should_profile()) {
            auto cpu_time_stop  = thread_cpu_time().count();
            auto wall_time_stop = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                      std::chrono::high_resolution_clock::now().time_since_epoch())
                                      .count();

            spdlog::get("illixr")->info("[cpu_timer]  cpu_timer,{},{},{},{},{},{}", name_, serial_no_, wall_time_start_,
                                        wall_time_stop, cpu_time_start_, cpu_time_stop);
        }
    }

private:
    const std::string name_;
    const std::size_t serial_no_;
    std::size_t       wall_time_start_;
    std::size_t       cpu_time_start_;
};

#define PRINT_CPU_TIME_FOR_THIS_BLOCK(name) \
    print_timer<decltype((thread_cpu_time))> PRINT_CPU_TIME_FOR_THIS_BLOCK{name, thread_cpu_time};

#define PRINT_WALL_TIME_FOR_THIS_BLOCK(name)                                                         \
    print_timer<decltype((std::chrono::high_resolution_clock::now))> PRINT_WALL_TIME_FOR_THIS_BLOCK{ \
        name, std::chrono::high_resolution_clock::now};

#define PRINT_RECORD_FOR_THIS_BLOCK(name) print_timer2 PRINT_RECORD_FOR_THIS_BLOCK_timer{name};

/**
 * @brief Use this in place of std::thread(...) to print times.
 */
template<class Function, class... Args>
[[maybe_unused]] std::thread timed_thread(const std::string& account_name, Function&& f, Args&&... args) {
    // Unfortunately we make copies of f and args.
    // According to StackOverflow, this is unavoidable.
    // See Sam Varshavchik's comment on https://stackoverflow.com/a/62380971/1078199
    return std::thread([=] {
        {
            PRINT_RECORD_FOR_THIS_BLOCK(account_name)
            std::invoke(f, args...);
        }
    });
}
