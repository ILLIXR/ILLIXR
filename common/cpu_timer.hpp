#pragma once

#include "error_util.hpp"

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

/**
 * @brief A C++ translation of [clock_gettime][1]
 *
 * [1]: https://linux.die.net/man/3/clock_gettime
 *
 */
static inline std::chrono::nanoseconds cpp_clock_gettime(clockid_t clock_id) {
    /* This ensures the compiler won't reorder this function call; Pretend like it has memory side-effects. */
    asm volatile(""
                 : /* OutputOperands */
                 : /* InputOperands */
                 : "memory" /* Clobbers */);
    struct timespec ts;

    RAC_ERRNO_MSG("cpu_timer before clock_gettime");

    if (clock_gettime(clock_id, &ts)) {
        throw std::runtime_error{std::string{"clock_gettime returned "} + strerror(errno)};
    }
    RAC_ERRNO_MSG("cpu_timer after clock_gettime");

    asm volatile(""
                 : /* OutputOperands */
                 : /* InputOperands */
                 : "memory" /* Clobbers */);
    return std::chrono::seconds{ts.tv_sec} + std::chrono::nanoseconds{ts.tv_nsec};
}

/**
 * @brief Gets the CPU time for the calling thread.
 */
static inline std::chrono::nanoseconds thread_cpu_time() {
    RAC_ERRNO_MSG("cpu_timer before cpp_clock_gettime");
    return cpp_clock_gettime(CLOCK_THREAD_CPUTIME_ID);
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
template<typename now_fn, typename time_point = decltype(std::declval<now_fn>()()),
         typename durationt = decltype(std::declval<time_point>() - std::declval<time_point>())>
class timer {
public:
    timer(const now_fn& now, durationt& _duration)
        : _p_now{now}
        , _p_duration{_duration} {
        _p_start = _p_now();
    }

    ~timer() {
        _p_duration = _p_now() - _p_start;
    }

private:
    const now_fn& _p_now;
    durationt&    _p_duration;
    time_point    _p_start;
};

template<typename Duration, typename Out = decltype(std::declval<Duration>().count())>
typename std::enable_if<std::is_integral<Out>::value, Out>::type count_duration(Duration t) {
    return std::chrono::duration_cast<std::chrono::nanoseconds, typename Duration::rep, typename Duration::period>(t).count();
}

template<typename Duration>
typename std::enable_if<std::is_integral<Duration>::value, Duration>::type count_duration(Duration t) {
    return t;
}

/**
 * @brief Like timer, but prints the output.
 *
 * See PRINT_CPU_TIME_FOR_THIS_BLOCK(name)
 *
 */
template<typename now_fn, typename time_point = decltype(std::declval<now_fn>()()),
         typename duration = decltype(std::declval<time_point>() - std::declval<time_point>())>
class print_timer {
private:
    class print_in_destructor {
    public:
        print_in_destructor(const std::string& account_name, const duration& _duration)
            : _p_account_name{account_name}
            , _p_duration{_duration} { }

        ~print_in_destructor() {
            // std::ostringstream os;
            // os << "cpu_timer," << _p_account_name << "," << count_duration<duration>(_p_duration) << "\n";
            if (rand() % 100 == 0) {
#ifndef NDEBUG
                std::cout << "cpu_timer.hpp is DEPRECATED. See logging.hpp.\n";
#endif
            }
        }

    private:
        const std::string _p_account_name;
        const duration&   _p_duration;
    };

    // NOTE that the destructors get called in reverse order!
    // This is important, because _p_timer's destructor records the timing information
    // Then, _p_print_in_destructor prints it
    // Then, we can destroy _p_duration.
    duration                                  _p_duration;
    const print_in_destructor                 _p_print_in_destructor;
    const timer<now_fn, time_point, duration> _p_timer;

public:
    print_timer(const std::string& name, const now_fn& now)
        : _p_print_in_destructor{name, _p_duration}
        , _p_timer{now, _p_duration} { }
};

static std::size_t gen_serial_no() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch())
        .count();
}

class should_profile_class {
public:
    should_profile_class() {
        const char* ILLIXR_STDOUT_METRICS = getenv("ILLIXR_STDOUT_METRICS");
        actually_should_profile           = ILLIXR_STDOUT_METRICS && (strcmp(ILLIXR_STDOUT_METRICS, "y") == 0);
    }

    bool operator()() {
        return actually_should_profile;
    }

private:
    bool actually_should_profile;
};

static should_profile_class should_profile;

class print_timer2 {
private:
    const std::string name;
    const std::size_t serial_no;
    std::size_t       wall_time_start;
    std::size_t       cpu_time_start;

public:
    print_timer2(std::string name_)
        : name{name_}
        , serial_no{should_profile() ? gen_serial_no() : std::size_t{0}}
        , wall_time_start{should_profile() ? std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                 std::chrono::high_resolution_clock::now().time_since_epoch())
                                                 .count()
                                           : std::size_t{0}}
        , cpu_time_start{should_profile() ? thread_cpu_time().count() : std::size_t{0}} { }

    ~print_timer2() {
        if (should_profile()) {
            auto cpu_time_stop  = thread_cpu_time().count();
            auto wall_time_stop = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                      std::chrono::high_resolution_clock::now().time_since_epoch())
                                      .count();

            std::cout << "cpu_timer," << name << "," << serial_no << "," << wall_time_start << "," << wall_time_stop << ","
                      << cpu_time_start << "," << cpu_time_stop << "\n";
        }
    }
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
std::thread timed_thread(const std::string& account_name, Function&& f, Args&&... args) {
    // Unfortunately we make copies of f and args.
    // According to StackOverflow, this is unavoidable.
    // See Sam Varshavchik's comment on https://stackoverflow.com/a/62380971/1078199
    return std::thread([=] {
        {
            PRINT_RECORD_FOR_THIS_BLOCK(account_name);
            std::invoke(f, args...);
        }
    });
}
