#pragma once

#include <atomic>
#include <iostream>
#include <future>
#include <algorithm>
#include "plugin.hpp"
#include "cpu_timer.hpp"

namespace ILLIXR {

	const record_header __threadloop_iteration_start_header {
		"threadloop_iteration_start",
		{
			{"plugin_id", typeid(std::size_t)},
			{"iteration_no", typeid(std::size_t)},
			{"cpu_time", typeid(std::chrono::nanoseconds)},
			{"wall_time", typeid(std::chrono::high_resolution_clock::time_point)},
		},
	};
	const record_header __threadloop_iteration_stop_header {
		"threadloop_iteration_stop",
		{
			{"plugin_id", typeid(std::size_t)},
			{"iteration_no", typeid(std::size_t)},
			{"cpu_time", typeid(std::chrono::nanoseconds)},
			{"wall_time", typeid(std::chrono::high_resolution_clock::time_point)},
		},
	};
	const record_header __threadloop_skip_start_header {
		"threadloop_skip_start",
		{
			{"plugin_id", typeid(std::size_t)},
			{"iteration_no", typeid(std::size_t)},
			{"skip_no", typeid(std::size_t)},
			{"cpu_time", typeid(std::chrono::nanoseconds)},
			{"wall_time", typeid(std::chrono::high_resolution_clock::time_point)},
		},
	};
	const record_header __threadloop_skip_stop_header {
		"threadloop_skip_stop",
		{
			{"plugin_id", typeid(std::size_t)},
			{"iteration_no", typeid(std::size_t)},
			{"skip_no", typeid(std::size_t)},
			{"cpu_time", typeid(std::chrono::nanoseconds)},
			{"wall_time", typeid(std::chrono::high_resolution_clock::time_point)},
		},
	};

/**
 * @brief A reusable threadloop for plugins.
 *
 * The thread continuously runs `_p_one_iteration()` and is stopable by `stop()`.
 *
 * This factors out the common code I noticed in many different plugins.
 */
class threadloop : public plugin {
public:
	threadloop(std::string name_, phonebook* pb_) : plugin(name_, pb_) { }

	/**
	 * @brief Starts the thread.
	 */
	virtual void start() override {
		plugin::start();
		_m_thread = std::thread(std::bind(&threadloop::thread_main, this));
	}

	/**
	 * @brief Stops the thread.
	 */
	virtual void stop() override {
		if (! _m_terminate.load()) {
			_m_terminate.store(true);
			_m_thread.join();
			plugin::stop();
		} else {
			std::cerr << "You called stop() on this plugin twice." << std::endl;
		}
	}

	virtual ~threadloop() override {
		if (!_m_terminate.load()) {
			std::cerr << "You didn't call stop() before destructing this plugin." << std::endl;
			abort();
		}
	}

protected:
	std::size_t iteration_no = 0;
	std::size_t skip_no = 0;

private:
	void thread_main() {
		metric_coalescer it_start {metric_logger};
		metric_coalescer it_stop  {metric_logger};
		metric_coalescer skip_start {metric_logger};
		metric_coalescer skip_stop  {metric_logger};

		_p_thread_setup();

		while (!should_terminate()) {

			skip_start.log(record{&__threadloop_skip_start_header, {
				{id},
				{iteration_no},
				{skip_no},
				{thread_cpu_time()},
				{std::chrono::high_resolution_clock::now()},
			}});
			skip_option s = _p_should_skip();
			skip_stop.log(record{&__threadloop_skip_stop_header , {
				{id},
				{iteration_no},
				{skip_no},
				{thread_cpu_time()},
				{std::chrono::high_resolution_clock::now()},
			}});

			switch (s) {
			case skip_option::skip_and_yield:
				std::this_thread::yield();
				++skip_no;
				break;
			case skip_option::skip_and_spin:
				++skip_no;
				break;
			case skip_option::run:
				it_start.log(record{&__threadloop_iteration_start_header, {
					{id},
					{iteration_no},
					{thread_cpu_time()},
					{std::chrono::high_resolution_clock::now()},
				}});
				_p_one_iteration();
				it_stop .log(record{&__threadloop_iteration_stop_header, {
					{id},
					{iteration_no},
					{thread_cpu_time()},
					{std::chrono::high_resolution_clock::now()},
				}});
				++iteration_no;
				skip_no = 0;
				break;
			case skip_option::stop:
				stop();
				break;
			}
		}
	}

protected:

	enum class skip_option {
		/// Run iteration NOW. Only then does CPU timer begin counting.
		run,

		/// AKA "busy wait". Skip but try again very quickly.
		skip_and_spin,

		/// Yielding gives up a scheduling quantum, which is determined by the OS, but usually on
		/// the order of 1-10ms. This is nicer to the other threads in the system.
		skip_and_yield,

		/// Calls stop.
		stop,
	};

	/**
	 * @brief Gets called in a tight loop, to gate the invocation of `_p_one_iteration()`
	 */
	virtual skip_option _p_should_skip() { return skip_option::run; }

	/**
	 * @brief Gets called at setup time, from the new thread.
	 */
	virtual void _p_thread_setup() { }

	/**
	 * @brief Override with the computation the thread does every loop.
	 *
	 * This gets called in rapid succession.
	 */
	virtual void _p_one_iteration() = 0;

	/**
	 * @brief Whether the thread has been asked to terminate.
	 *
	 * Check this before doing long-running computation; it makes termination more responsive.
	 */
	bool should_terminate() {
		return _m_terminate.load();
	}

	/**
	 * @brief Sleeps until a roughly @p stop.
	 *
	 * We attempt to still be somewhat responsive to `stop()` and to be more accurate than
	 * stdlib's `sleep`, by sleeping for the deadline in chunks.
	 */
	void reliable_sleep(std::chrono::high_resolution_clock::time_point stop) {
		while ((!should_terminate()) && std::chrono::high_resolution_clock::now() < stop - std::chrono::milliseconds{4}) {
			std::this_thread::yield();
		}
	}

private:
	std::atomic<bool> _m_terminate {false};

	std::thread _m_thread;
};

}
