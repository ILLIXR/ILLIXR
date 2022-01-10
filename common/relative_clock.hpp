#pragma once
#include <chrono>
#include <ratio>
#include "phonebook.hpp"

namespace ILLIXR {

/**
 * Mimick of `std::chrono::time_point<Clock, Rep>` [1].
 *
 * Can't use `std::chrono::time_point<Clock, Rep>`, because the `Clock` must satisfy the named
 * requirements of the Clock interface [2], but `RelativeClock` cannot for the aforementioned
 * reasons. Instead, we will mimick the interface of [1] here.
 *
 * [1]: https://en.cppreference.com/w/cpp/chrono/time_point
 * [2]: https://en.cppreference.com/w/cpp/named_req/Clock
 */
class time_point<class duration = std::chrono::duration<long, std::nano>> {
public:
	constexpr time_point() { }
	constexpr explicit time_point(const duration& time_since_epoch) : _m_time_since_epoch{time_since_epoch} { }
	template<class Duration2>
	constexpr time_point(const time_point<duration2>& t) : _m_time_since_epoch{std::chrono::duration_cast<duration>(t._m_time_since_epoch)} { }
	duration time_since_epoch() const { return _m_time_since_epoch; }
private:
	duration _m_time_since_epoch;
};

template<class D1, class D2>
constexpr typename std::common_type<D1,D2>::type
operator-(const time_point<D1>& lhs, const time_point<D2>& rhs) {
	return lhs.time_since_epoch() - rhs.time_since_epoch();
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

	using rep = long;
	using period = std::nano;
	using duration = std::chrono::duration<rep, period>;
	using time_point = time_point<duration>;
	static constexpr bool is_steady = true;
	static_assert(std::chrono::steady_clock::is_steady);

	time_point now() const {
		assert(_m_start > std::chrono::steady_clock::time_point{} && "Can't call now() before this clock has been start()ed.");
		return time_point{std::chrono::steady_clock::now() - _m_start};
	}
	int64_t absolute_ns(time_point relative) {
		// return std::chrono::nanoseconds{(_m_start + relative).time_since_epoch()}.count();
		return std::chrono::nanoseconds{_m_start.time_since_epoch()}.count() + std::chrono::nanoseconds{relative.time_since_epoch()}.count();
	}

	/**
	 * @brief Starts the clock. All times are relative to this point.
	 */
	void start() {
		_m_start = std::chrono::steady_clock::now();
	}

private:
	std::chrono::steady_clock::time_point _m_start;
};

using duration = RelativeClock::duration;

template <typename unit = std::ratio<1>> double duration2double(duration dur) {
	return std::chrono::duration<double, unit>{dur}.count();
}

constexpr duration freq2period(double fps) {
	return duration{static_cast<size_t>(std::chrono::nanoseconds{std::chrono::seconds{1}}.count() / fps)};
}

}
