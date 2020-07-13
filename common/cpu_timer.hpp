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
	/* This ensures the compiler won't reorder this function call; Pretend like it has memory side-effects. */
	asm volatile (""
				  : /* OutputOperands */
				  : /* InputOperands */
				  : "memory" /* Clobbers */);
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


#define PRINT_CPU_TIME_FOR_THIS_BLOCK(name)

#define PRINT_WALL_TIME_FOR_THIS_BLOCK(name)

/**
 * @brief Use this in place of std::thread(...) to print times.
 */
template< class Function, class... Args >
std::thread timed_thread([[maybe_unused]]std::string account_name, Function&& f, Args&&... args) {
    // Unfortunately we make copies of f and args.
    // According to StackOverflow, this is unavoidable.
    // See Sam Varshavchik's comment on https://stackoverflow.com/a/62380971/1078199
    return std::thread([=] {
        std::invoke(f, args...);
    });
}
