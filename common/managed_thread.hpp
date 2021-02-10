#pragma once

#include <cassert>
#include <thread>
#include <cerrno>
#include <functional>
#include <atomic>
#include <unistd.h>
#include <sys/syscall.h>
#include <sched.h>

namespace ILLIXR {

pid_t gettid() { return syscall(SYS_gettid); }

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
 * @brief Base class for threads.
 *
 * This has the advantage that it naturally isolates the thread-local data. Most of the time,
 * private/protected data and methods can only be accessed from INSIDE the thread, while public
 * methods can be called from any thread.
 *
 * It also takes care of being responsive to the shutdown signal. When the thread goes out of scope
 * (perhaps by deleting a pointer), it gets joined.
 *
 * \code{.cpp}
 * class InnerThread : public ManagedThread {
 * protected:
 *     void on_start() {
 *         std::cout << "Starting" << std::endl;
 *     }
 *     void body() {
 *         std::cout << "Running " << get_iteration() << std::endl;
 *         sleep(std::chrono::milliseconds{100});
 *     }
 *     void on_stop() {
 *         std::cout << "Stopping" << std::endl;
 *     }
 * };
 *
 *
 * // Somewhere else
 * {
 *     ChildCallParent<InnerThread> the_thread;
 *     std::cout << "TID = " << the_thread.get_tid() << std::endl;
 *     sleep(1);
 *     // thread gets stopped here, since the_thread goes out of scope.
 * }
 * std::cout << "Here" << std::endl;
 *
 *
 * // Possible output:
 * // Starting
 * // TID = 12345
 * // Running 1
 * // Running 2
 * // Running 3
 * // Stopping
 * // Here
 * \endcode
 */
class ManagedThread {
private:
	std::atomic<bool> _m_stop {false};
	size_t _m_iterations;
	std::thread _m_thread;
	pid_t _m_pid;
	Event _m_has_pid;
	Event _m_is_started;

	void thread_main() {
		_m_pid = ::gettid();
		_m_has_pid.set();

		on_start();
		_m_is_started.set();

		while (!should_stop()) {
			body();
			++_m_iterations;
		}

		on_stop();
	}

	template <class Rep, class Period>
	bool actual_sleep(const std::chrono::duration<Rep, Period>& duration) const {
		if (duration > 0) {
			auto seconds = std::chrono::seconds{duration};
			auto remaining_duration = duration - seconds;
			auto nanoseconds = std::chrono::nanoseconds{remaining_duration};
			struct timespec duration_timespec {
				.tv_sec = seconds.count(),
				.tv_nsec = nanoseconds.count(),
			};
			struct timespec remaining;
	
			assert(errno = 0);
			errno = 0;
			int ret = nanosleep(&duration_timespec, &remaining);
			if (ret != 0) {
				std::system_error err {std::make_error_code(std::errc{errno})};
				errno = 0;
				throw err;
			}
			errno = 0;
		}
	}

	/**
	 * @brief Called on thread start.
	 *
	 * virtual methods should be private; they can still be overriden by the derived class.
	 * http://www.gotw.ca/publications/mill18.htm
	 */
	virtual void on_start() { }
	/**
	 * @brief Called in the thread's loop
	 *
	 * virtual methods should be private; they can still be overriden by the derived class.
	 * http://www.gotw.ca/publications/mill18.htm
	 */
	virtual void body() { }
	/**
	 * @brief Called on thread stop.
	 *
	 * virtual methods should be private; they can still be overriden by the derived class.
	 * http://www.gotw.ca/publications/mill18.htm
	 */
	virtual void on_stop() { }

protected:
	/**
	 * @brief Sleeps for duration, but could be interrupted if a stop is requested.
	 *
	 * @returns Tests if sleep was not interrupted.
	 *
	 * Will be overriden by managed_thread.
	 */
	template <class Clock, class Rep, class Period>
	bool sleep(const std::chrono::duration<Rep, Period>& duration) const {
		auto start = Clock::now();
		auto period = std::chrono::milliseconds{10};
		std::chrono::nanoseconds remaining;
		while ((remaining = start + duration - Clock::now()) > period) {
			// More waiting to do
			// Check should_stop first
			if (should_stop()) { return false; }
			actual_sleep(period);
		}
		if (should_stop()) { return false; }
		if (remaining > std::chrono::nanoseconds{1}) {
			actual_sleep(remaining);
		}
		return true;
	}

	/**
	 * @brief Returns the number of iterations
	 */
	size_t get_iterations() const { return _m_iterations; }

public:
	/**
	 * @returns Tests if a stop is requested.
	 */
	bool should_stop() const { return _m_stop.load(); }

	/**
	 * @brief Request a thread stop.
	 */
	void stop() { _m_stop.store(true); }

	/**
	 * @brief Wait for the thread to be started.
	 */
	void wait_for_start() const { return _m_is_started.wait(); }

	/**
	 * @brief Gets the Linux-specific thread ID.
	 *
	 * This can be used with Linux-specific syscalls, such as sched_*.
	 */
	pid_t get_tid() const {
		_m_has_pid.wait();
		return _m_pid;
	}

	/**
	 * @brief See sched_setaffinity.
	 */
	void set_cpu_affinity(const std::vector<unsigned int>& cores) const {
		cpu_set_t mask;
		CPU_ZERO(&mask);
		for (unsigned int core : cores) { CPU_SET(core, &mask); }

		assert(errno == 0);
		errno = 0;
		int ret = sched_setaffinity(get_tid(), sizeof(mask), &mask);

		if (ret != 0) {
			std::system_error err {std::make_error_code(std::errc{errno}), "sched_setaffinity"};
			errno = 0;
			throw err;
		}
		errno = 0;
	}

	/**
	 * @brief See sched_setpriority.
	 */
	void set_priority(int priority) const {
		struct sched_param sp = {.sched_priority = priority};

		assert(errno == 0);
		errno = 0;
		int ret = sched_setscheduler(get_tid(), SCHED_FIFO, &sp);

		if (ret != 0) {
			std::system_error err {std::make_error_code(std::errc{errno}), "sched_setscheduler"};
			errno = 0;
			// Note, this will fail unless run as root.
			throw err;
		}
		errno = 0;
	}

	virtual ~ManagedThread() { }

public:
	/**
	 * The parent's constructor happens before the child's, and the parent's destructor
	 * happens after the child's (like russian dolls). However, 
	 *
	 * The thread needs to be launched AFTER derived class's constructor, so that it can call its
	 * virtual methods. Calling in the last line of constructor works.
	 */
	void __after_child_constructor() {
		_m_thread = std::thread{[this] {this->thread_main(); }};
	}

	/**
	 * Like after_derived_constructor, since this method stops a thread which calls virtual methods
	 * defined in the derived class, it needs to be run BEFORE derived class's destructor (first
	 * line of destructor), because the thread might touch managed_thread_impl's private
	 * methods.
	 */
	void __before_child_destructor() {
		_m_stop.store(true);
		_m_thread.join();
	}

	/**
	 * Your constructor must call __after_derived_constructor after everything else, and your
	 * destructor must call __before_derived_destructor before everything else.
	 *
	 * Override this with a no-op, if-and-only-if you fulfill this contract.
	 *
	 * Since failure to override this indicates you don't fulfill this contract, and the compiler
	 * will tell you your class cannot be constructed until you do (it's a type tag).
	 *
	 * You can do this by hand, or just wrap your class in ChildCallParent.
	 *
	 */
	virtual void __i_promise_to_call_after_constructor_and_before_destructor() = 0;
};

/**
 * Say you want to call a derived class's virtual method in a base class's constructor.
 *
 * Normal C++ can't natively do this, because the derived class has not yet been constructed.
 *
 * The famous ISOCPP FAQ recommends calling "init()" immediately after construction.
 * https://isocpp.org/wiki/faq/strange-inheritance#calling-virtuals-from-ctors
 *
 * That's the beginning of a solution, but you have to remember to do it. This class automates that
 * (no need to remember anything special).
 *
 * It does this by being a "mixin" class.
 * https://www.fluentcpp.com/2017/12/12/mixin-classes-yang-crtp/
 *
 * If TWO parents need this functionality (suppose C inherits B and B inherits A, where A and B want to call
 * virtual methods in C), they can be "chained." B::__after_child_constructor should override and
 * call A::__after_child_constructor, and likewise for the destructor.
 *
 * This class is final, so it can't be subclassed.
 */
template <typename Child>
class ChildCallParent final : Child {
public:
	template <class... T>
	ChildCallParent(T... t)
		: Child(t...)
	{
		// Last constructor to run.
		child().__after_child_constructor();
	}

	~ChildCallParent() {
		// First destructor to run.
		child().__before_child_destructor();
	}

	const Child& child() const { return static_cast<Child&>(*this); }
	Child& child() { return static_cast<Child&>(*this); }

protected:
	virtual void __i_promise_to_call_after_constructor_and_before_destructor() override { }
};

/**
 * @brief If you don't need to use the mixin functions of the base managed_thread class.
 */
class ManagedThreadFunctions : public ManagedThread {
private:
	std::function<void()> _m_body;
	std::function<void()> _m_on_start;
	std::function<void()> _m_on_stop;
	virtual void on_stop() override { if (_m_on_stop) _m_on_stop(); }
	virtual void on_start() override { if (_m_on_start) _m_on_start(); }
	virtual void body() override { _m_body(); }
	
public:
	ManagedThreadFunctions(
		std::function<void()> body,
		std::function<void()> on_start = std::function<void()>{},
		std::function<void()> on_stop = std::function<void()>{}
	) : _m_body{body}
	  , _m_on_start{on_start}
	  , _m_on_stop{on_stop}
	{ }
};

} // namespace ILLIXR
