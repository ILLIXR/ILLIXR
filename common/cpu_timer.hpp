#pragma once

#include <string>
#include <sstream>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <iostream>
#include <functional>
#include <thread>

/**
 * @brief A C++ translation of [clock_gettime][1]
 *
 * [1]: https://linux.die.net/man/3/clock_gettime
 *
 */
static inline std::chrono::nanoseconds
cpp_clock_gettime(clockid_t clock_id) {
    struct timespec ts;
    if (clock_gettime(clock_id, &ts)) {
        throw std::runtime_error{std::string{"clock_gettime returned "} + strerror(errno)};
    }
    return std::chrono::seconds{ts.tv_sec} + std::chrono::nanoseconds{ts.tv_nsec};
}

/**
 * @brief Gets the CPU time for the calling thread.
 */
static inline std::chrono::nanoseconds
thread_cpu_time() {
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
template <
    typename now_fn,
    typename time_point = decltype(std::declval<now_fn>()()),
    typename durationt = decltype(std::declval<time_point>() - std::declval<time_point>())
    >
class timer {
public:
    timer(const now_fn& now, durationt& _duration)
        : _p_now{now} , _p_duration{_duration} {
        _p_start = _p_now();
    }

    ~timer() {
        _p_duration = _p_now() - _p_start;
    }

private:
    const now_fn& _p_now;
    durationt& _p_duration;
    time_point _p_start;
};

template <typename Duration, typename Out = decltype(std::declval<Duration>().count())>
typename std::enable_if<std::is_integral<Out>::value, Out>::type
count_duration(Duration t) {
	return std::chrono::duration_cast<
		std::chrono::nanoseconds,
		typename Duration::rep,
		typename Duration::period
		>(t).count();
}

template <typename Duration>
typename std::enable_if<std::is_integral<Duration>::value, Duration>::type
count_duration(Duration t) {
	return t;
}

/**
 * @brief Like timer, but prints the output.
 *
 * See PRINT_CPU_TIME_FOR_THIS_BLOCK(name)
 *
 */
template <
    typename now_fn,
    typename time_point = decltype(std::declval<now_fn>()()),
    typename duration = decltype(std::declval<time_point>() - std::declval<time_point>())
    >
class print_timer {
public:
private:
    class print_in_destructor {
    public:
        print_in_destructor(const std::string& account_name, const duration& _duration)
            : _p_account_name{account_name}
            , _p_duration{_duration}
        { }
        ~print_in_destructor() {
            std::ostringstream os;
            os << "cpu_timer," << _p_account_name << "," << count_duration<duration>(_p_duration) << "\n";
            std::cout << os.str() << std::flush;
        }
    private:
        const std::string _p_account_name;
        const duration& _p_duration;
    };

    // NOTE that the destructors get called in reverse order!
    // This is important, because _p_timer's destructor records the timing information
    // Then, _p_print_in_destructor prints it
    // Then, we can destroy _p_duration.
    duration _p_duration;
    const print_in_destructor _p_print_in_destructor;
    const timer<now_fn, time_point, duration> _p_timer;
public:
    print_timer(const std::string& name, const now_fn& now)
        : _p_print_in_destructor{name, _p_duration}
        , _p_timer{now, _p_duration}
    { }
};

#define PRINT_CPU_TIME_FOR_THIS_BLOCK(name)                                 \
    print_timer<decltype((thread_cpu_time))> PRINT_CPU_TIME_FOR_THIS_BLOCK {name, thread_cpu_time};

#define PRINT_WALL_TIME_FOR_THIS_BLOCK(name)                                    \
    print_timer<decltype((std::chrono::high_resolution_clock::now))> PRINT_WALL_TIME_FOR_THIS_BLOCK {name, std::chrono::high_resolution_clock::now};

/**
 * @brief Use this in place of std::thread(...) to print times.
 */
template< class Function, class... Args >
std::thread timed_thread(const std::string& account_name, Function&& f, Args&&... args) {
    // Unfortunately we make copies of f and args.
    // According to StackOverflow, this is unavoidable.
    // See Sam Varshavchik's comment on https://stackoverflow.com/a/62380971/1078199
    return std::thread([=] {
        {   PRINT_CPU_TIME_FOR_THIS_BLOCK(account_name);
            std::invoke(f, args...);
        }
    });
}
