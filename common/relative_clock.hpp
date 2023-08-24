#pragma once
#include "phonebook.hpp"

#include <chrono>
#include <ratio>

namespace ILLIXR {

/**
 * Mimick of `std::chrono::time_point<Clock, Rep>` [1].
 *
 * Can't use `std::chrono::time_point<Clock, Rep>`, because the `Clock` must satisfy the Clock interface [2],
 * but `RelativeClock` cannot satisfy this interface because `RelativeClock::now()`
 * is a stateful (instance method) not pure (class method).
 * Instead, we will mimick the interface of [1] here.
 *
 * [1]: https://en.cppreference.com/w/cpp/chrono/time_point
 * [2]: https://en.cppreference.com/w/cpp/named_req/Clock
 */
using _clock_rep      = long;
using _clock_period   = std::nano;
using _clock_duration = std::chrono::duration<_clock_rep, _clock_period>;

class time_point {
public:
    using duration = _clock_duration;

    time_point() { }

    constexpr explicit time_point(const duration& time_since_epoch)
        : _m_time_since_epoch{time_since_epoch} { }

    duration time_since_epoch() const {
        return _m_time_since_epoch;
    }

    time_point& operator+=(const duration& d) {
        this->_m_time_since_epoch += d;
        return *this;
    }

    time_point& operator-=(const duration& d) {
        this->_m_time_since_epoch -= d;
        return *this;
    }

private:
    duration _m_time_since_epoch;
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
class RelativeClock : public phonebook::service {
public:
    using rep                       = _clock_rep;
    using period                    = _clock_period;
    using duration                  = _clock_duration;
    using time_point                = time_point;
    static constexpr bool is_steady = true;
    static_assert(std::chrono::steady_clock::is_steady);

    time_point now() const {
        assert(this->is_started() && "Can't call now() before this clock has been start()ed.");
        return time_point{std::chrono::steady_clock::now() - _m_start};
    }

    int64_t absolute_ns(time_point relative) {
        return std::chrono::nanoseconds{_m_start.time_since_epoch()}.count() +
            std::chrono::nanoseconds{relative.time_since_epoch()}.count();
    }

    /**
     * @brief Starts the clock. All times are relative to this point.
     */
    void start() {
        _m_start = std::chrono::steady_clock::now();
    }

    /**
     * @brief Check if the clock is started.
     */
    bool is_started() const {
        return _m_start > std::chrono::steady_clock::time_point{};
    }

    /**
     * @brief Get the start time of the clock.
     */
    time_point start_time() const {
        return time_point{_m_start.time_since_epoch()};
    }

private:
    std::chrono::steady_clock::time_point _m_start;
};

using duration = RelativeClock::duration;

template<typename unit = std::ratio<1>>
double duration2double(duration dur) {
    return std::chrono::duration<double, unit>{dur}.count();
}

constexpr duration freq2period(double fps) {
    return duration{static_cast<size_t>(std::chrono::nanoseconds{std::chrono::seconds{1}}.count() / fps)};
}

} // namespace ILLIXR
