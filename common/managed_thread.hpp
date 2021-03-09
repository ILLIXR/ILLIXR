#pragma once

#include <cassert>
#include <cstring>
#include <thread>
#include <cerrno>
#include <functional>
#include <atomic>
#include <unistd.h>
#include <sys/syscall.h>
#include <sched.h>

namespace ILLIXR {

[[maybe_unused]] static pid_t get_tid() { return syscall(SYS_gettid); }

/**
 * @brief A boolean condition-variable.
 *
 * Inspired by https://docs.python.org/3/library/threading.html#event-objects
 */
class Event {
private:
	mutable std::mutex _m_mutex;
	mutable std::condition_variable _m_cv;
	bool _m_value;

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

/**
 * @brief An object that manages a std::thread; it joins and exits when the object gets destructed.
 */
class managed_thread {
private:
	std::atomic<bool> _m_stop {false};
	std::thread _m_thread;
	std::function<void()> _m_body;
	std::function<void()> _m_on_start;
	std::function<void()> _m_on_stop;
	cpu_timer::TypeEraser _m_info;
	pid_t pid;
	bool thread_is_started;
	std::condition_variable thread_is_started_cv;
	std::mutex thread_is_started_mutex;

	void thread_main() {
		assert(_m_body);
		CPU_TIMER_TIME_FUNCTION_INFO(_m_info);
		pid = get_tid();
		{
			std::unique_lock<std::mutex> lock {thread_is_started_mutex};
			thread_is_started = true;
			thread_is_started_cv.notify_all();
		}
		if (_m_on_start) {
			_m_on_start();
		}
		while (!this->_m_stop.load()) {
			_m_body();
		}
		if (_m_on_stop) {
			_m_on_stop();
		}
	}

public:

	/**
	 * @brief Constructs a non-startable thread
	 */
	managed_thread() noexcept { }

	/**
	 * @brief Constructs a startable thread
	 *
	 * @p on_stop is called once (if present)
	 * @p on_start is called as the thread is joining
	 * @p body is called in a tight loop
	 */
       managed_thread(
               std::function<void()> body,
               std::function<void()> on_start = std::function<void()>{},
               std::function<void()> on_stop = std::function<void()>{},
               cpu_timer::TypeEraser info = cpu_timer::type_eraser_default
       ) noexcept
                : _m_body{body}
                , _m_on_start{on_start}
                , _m_on_stop{on_stop}
				, _m_info{std::move(info)}
				, pid{0}
        { }

	/**
	 * @brief Stops a thread, if necessary
	 */
	~managed_thread() noexcept {
		if (get_state() == state::running) {
			stop();
		}
		assert(get_state() == state::stopped || get_state() == state::startable || get_state() == state::nonstartable);
		// assert(!_m_thread.joinable());
	}

	/// Possible states for a managed_thread
	enum class state {
		nonstartable = 0,
		startable = 1,
		running = 2,
		stopped = 3,
	};

	/**
	 */
	state get_state() const {
		bool stopped = _m_stop.load();
		if (false) {
		} else if (!_m_body) {
			return state::nonstartable;
		} else if (!stopped && pid == 0) {
			return state::startable;
		} else if (!stopped && pid != 0) {
			return state::running;
		} else if (stopped) {
			return state::stopped;
		} else {
			throw std::logic_error{"Unknown state"};
		}
	}

	/**
	 * @brief Moves a managed_thread from startable to running
	 */
	void start() {
		assert(get_state() == state::startable);
		thread_is_started = false;
		_m_thread = std::thread{&managed_thread::thread_main, this};

		{
			std::unique_lock<std::mutex> lock {thread_is_started_mutex};
			if (!thread_is_started) {
				thread_is_started_cv.wait(lock, [this]{	return thread_is_started;});
			}
		}
#ifndef NDEBUG
		std::cerr << "thread_is_started = " << thread_is_started << "\n";
		std::cerr << "PID = " << pid << "\n";
		std::cerr << "state = " << int(get_state()) << "\n";
		assert(get_state() == state::running);
#endif
	}

	void request_stop() {
		_m_stop.store(true);
	}

	/**
	 * @brief Moves a managed_thread from running to stopped
	 */
	void stop() {
		assert(get_state() == state::running);
		_m_stop.store(true);
		_m_thread.join();
		assert(get_state() == state::stopped);
	}

	pid_t get_pid() const {
		assert(get_state() == state::running);
		return pid;
	}

	void set_cpu(size_t core) const {
		errno = 0;

		cpu_set_t mask;
		CPU_ZERO(&mask);
		CPU_SET(core, &mask);

		[[maybe_unused]] int ret = sched_setaffinity(get_pid(), sizeof(mask), &mask);

		if (ret) {
			int ferrno = errno;
			errno = 0;
			std::system_error err (std::make_error_code(std::errc(ferrno)), "sched_setaffinity");
			std::cerr << ret << " " << ferrno << " " << err.what() << "\n";
			throw err;
		}
	}

	void set_priority(int priority) const {
		struct sched_param sp = { .sched_priority = priority,};

		errno = 0;

		[[maybe_unused]] int ret = sched_setscheduler(get_pid(), SCHED_FIFO, &sp);

		if (ret && std::getenv("ILLIXR_IGNORE_SCHED_SETSCHEDULER") && strcmp(std::getenv("ILLIXR_IGNORE_SCHED_SETSCHEDULER"), "y") != 0) {
			int ferrno = errno;
			errno = 0;
			std::system_error err (std::make_error_code(std::errc(ferrno)), "sched_setscheduler");
			std::cerr << ret << " " << ferrno << " " << err.what() << " " << strcmp(std::getenv("ILLIXR_IGNORE_SCHED_SETSCHEDULER"), "y") << " " << std::getenv("ILLIXR_IGNORE_SCHED_SETSCHEDULER") << "\n";
			throw err;
		}
	}
};

} // namespace ILLIXR
