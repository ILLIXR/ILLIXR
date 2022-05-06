#pragma once
#include <chrono>
#include <ratio>
#include "phonebook.hpp"

namespace ILLIXR {

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
	using time_point = std::chrono::time_point<RelativeClock>;
	static constexpr bool is_steady = true;
	static_assert(std::chrono::steady_clock::is_steady);

	time_point now() const {
		assert(_m_start > std::chrono::steady_clock::time_point{} && "Can't call now() before this clock has been start()ed.");
		return time_point{std::chrono::steady_clock::now() - _m_start};
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

using time_point = RelativeClock::time_point;
using duration = RelativeClock::duration;

template <typename unit = std::ratio<1>> double duration2double(duration dur) {
	return std::chrono::duration<double, unit>{dur}.count();
}

constexpr duration freq2period(double fps) {
	return duration{static_cast<size_t>(std::chrono::nanoseconds{std::chrono::seconds{1}}.count() / fps)};
}

}
