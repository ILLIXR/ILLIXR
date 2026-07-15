#pragma once

#include "phonebook.hpp"

#include <cassert>
#include <chrono>
#include <ratio>

namespace ILLIXR {

/**
 * Mimic of `std::chrono::time_point<Clock, Rep>` [1].
 *
 * Can't use `std::chrono::time_point<Clock, Rep>`, because the `Clock` must satisfy the Clock interface [2],
 * but `relative_clock` cannot satisfy this interface because `relative_clock::now()`
 * is a stateful (instance method) not pure (class method).
 * Instead, we will mimic the interface of [1] here.
 *
 * [1]: https://en.cppreference.com/w/cpp/chrono/time_point
 * [2]: https://en.cppreference.com/w/cpp/named_req/Clock
 */
using clock_rep_      = long long;
using clock_period_   = std::nano;
using clock_duration_ = std::chrono::duration<clock_rep_, clock_period_>;

class time_point {
public:
    using duration = clock_duration_;

    time_point() = default;

    constexpr explicit time_point(const duration& time_since_epoch)
        : time_since_epoch_{time_since_epoch} { }

    [[nodiscard]] duration time_since_epoch() const {
        return time_since_epoch_;
    }

    time_point& operator+=(const duration& d) {
        this->time_since_epoch_ += d;
        return *this;
    }

    time_point& operator-=(const duration& d) {
        this->time_since_epoch_ -= d;
        return *this;
    }

    duration time_since_epoch_;
};

inline time_point::duration operator-(const time_point& lhs, const time_point& rhs) {
    return lhs.time_since_epoch() - rhs.time_since_epoch();
}

inline time_point operator+(const time_point& pt, const time_point::duration& d) {
    return time_point(pt.time_since_epoch() + d);
}

inline time_point operator+(const time_point::duration& d, const time_point& pt) {
    return time_point(pt.time_since_epoch() + d);
}

inline bool operator<(const time_point& lhs, const time_point& rhs) {
    return lhs.time_since_epoch() < rhs.time_since_epoch();
}

inline bool operator>(const time_point& lhs, const time_point& rhs) {
    return lhs.time_since_epoch() > rhs.time_since_epoch();
}

inline bool operator<=(const time_point& lhs, const time_point& rhs) {
    return lhs.time_since_epoch() <= rhs.time_since_epoch();
}

inline bool operator>=(const time_point& lhs, const time_point& rhs) {
    return lhs.time_since_epoch() >= rhs.time_since_epoch();
}

inline bool operator==(const time_point& lhs, const time_point& rhs) {
    return lhs.time_since_epoch() == rhs.time_since_epoch();
}

inline bool operator!=(const time_point& lhs, const time_point& rhs) {
    return lhs.time_since_epoch() != rhs.time_since_epoch();
}

/**
 * @brief Relative clock for all of ILLIXR
 *
 * Please use this instead of std::chrono clocks; this way, you can fake real time without changing your code.
 *
 * It also eliminates the class of bugs relating to using absolute time instead of time-since-start.
 *
 * Unfortunately this can't satisfy [Clock][1]
 * because it needs to have data (namely _m_start) shared across link-time boundaries. There's no
 * clean way to do this with static variables, so instead I use instance variables and Phonebook.
 *
 * [1]: https://en.cppreference.com/w/cpp/named_req/Clock
 */
class relative_clock : public phonebook::service {
public:
    using duration = clock_duration_;
    static_assert(std::chrono::steady_clock::is_steady);

    [[nodiscard]] time_point now() const {
        assert(this->is_started() && "Can't call now() before this clock has been start()ed.");
        return time_point{std::chrono::steady_clock::now() - start_};
    }

    [[maybe_unused]] int64_t absolute_ns(time_point relative) {
        return std::chrono::nanoseconds{start_.time_since_epoch()}.count() +
            std::chrono::nanoseconds{relative.time_since_epoch()}.count();
    }

    /**
     * @brief Starts the clock. All times are relative to this point.
     */
    void start() {
        start_ = std::chrono::steady_clock::now();
    }

    /**
     * @brief Check if the clock is started.
     */
    [[nodiscard]] bool is_started() const {
        return start_ > std::chrono::steady_clock::time_point{};
    }

    /**
     * @brief Get the start time of the clock.
     */
    [[maybe_unused]] [[nodiscard]] time_point start_time() const {
        return time_point{start_.time_since_epoch()};
    }

private:
    std::chrono::steady_clock::time_point start_;
};

using duration = relative_clock::duration;

template<typename Unit = std::ratio<1>>
double duration_to_double(duration dur) {
    return std::chrono::duration<double, Unit>{dur}.count();
}

constexpr duration freq_to_period(double fps) {
    return duration{static_cast<size_t>(std::chrono::nanoseconds{std::chrono::seconds{1}}.count() / fps)};
}

} // namespace ILLIXR
