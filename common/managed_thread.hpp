#pragma once

#include <cassert>
#include <thread>
#include <cerrno>
#include <functional>
#include <atomic>
#include <unistd.h>
#include <sys/syscall.h>
#include <sched.h>

#define gettid() syscall(SYS_gettid)

namespace ILLIXR {

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
	pid_t pid;
	bool thread_is_started;
	std::condition_variable thread_is_started_cv;
	std::mutex thread_is_started_mutex;


	void thread_main() {
		assert(_m_body);
		pid = ::gettid();

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
	managed_thread(std::function<void()> body, std::function<void()> on_start = std::function<void()>{}, std::function<void()> on_stop = std::function<void()>{}) noexcept
		: _m_body{body}
		, _m_on_start{on_start}
		, _m_on_stop{on_stop}
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
		_m_thread = std::thread{&managed_thread::thread_main, this};
		{
			std::unique_lock<std::mutex> lock {thread_is_started_mutex};
			thread_is_started_cv.wait(lock, [this]{return thread_is_started;});
		}
		std::cerr << int(get_state()) << "\n";
		assert(get_state() == state::running);
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

	void set_priority(int priority) const {
		struct sched_param sp = { .sched_priority = priority,};

		{
			int ferrno = errno;
			errno = 0;
			if (ferrno != 0) {
				std::system_error err (std::make_error_code(std::errc(ferrno)), "Before sched_setscheduler");
				std::cerr << ferrno << " " << err.what() << "\n";
			}
		}

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
