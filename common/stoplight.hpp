#pragma once

#include <mutex>
#include <chrono>
#include <condition_variable>
#include "phonebook.hpp"

namespace ILLIXR {

/**
 * @brief A boolean condition-variable.
 *
 * Inspired by https://docs.python.org/3/library/threading.html#event-objects
 */
class Event {
private:
	mutable std::mutex _m_mutex;
	mutable std::condition_variable _m_cv;
	bool _m_value = false;

public:

	/**
	 * @brief Sets the condition-variable to new_value.
	 *
	 * Defaults to true, so that set() sets the bool.
	 */
	void set(bool new_value = true) {
		{
			std::lock_guard lock {_m_mutex};
			_m_value = new_value;
		}
		if (new_value) {
			_m_cv.notify_all();
		}
	}

	/**
	 * @brief Clears the condition-variable.
	 */
	void clear() { set(false); }

	/**
	 * @brief Test if is set without blocking.
	 */
	bool is_set() const {
		std::unique_lock<std::mutex> lock {_m_mutex};
		return _m_value;
	}

	/**
	 * @brief Wait indefinitely for the event to be set.
	 */
	void wait() const {
		std::unique_lock<std::mutex> lock {_m_mutex};
		// Check if we even need to wait
		if (_m_value) {
			return;
		}
		_m_cv.wait(lock, [this] { return _m_value; });
	}

	/**
	 * @brief Wait for the event to be set with a timeout.
	 *
	 * Returns whether the event was actually set.
	 */
	template <class Clock, class Rep, class Period>
	bool wait_timeout(const std::chrono::duration<Rep, Period>& duration) const {
		auto timeout_time = Clock::now() + duration;
		std::unique_lock<std::mutex> lock {_m_mutex};
		if (_m_value) {
			return true;
		}
		while (_m_cv.wait_until(lock, timeout_time) != std::cv_status::timeout) {
			if (_m_value) {
				return true;
			}
		}
		return false;
	}
};

class Stoplight : public phonebook::service {
public:
	void wait_for_ready() const {
		_m_ready.wait();
	}

	void ready() {
		_m_ready.set();
	}

	bool should_stop() const {
		return _m_stop.load();
	}

	void stop() {
		_m_stop.store(true);
	}
private:
	Event _m_ready;

	/*
	  I use an atomic instead of an event for this one, since it is being "checked" not "waited on."

	  Event uses std::condition_variable, which does not support
	  std::shared_mutex, so readers would need a std::unique_lock,
	  which would exclude other readers.
	*/
	std::atomic<bool> _m_stop {false};
};

} // namespace ILLIXR
