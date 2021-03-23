#pragma once

#include <atomic>
#include <iostream>
#include <future>
#include <algorithm>
#include "plugin.hpp"
#include "cpu_timer.hpp"
#include "stoplight.hpp"

namespace ILLIXR {

const record_header __threadloop_iteration_header {"threadloop_iteration", {
	{"plugin_id", typeid(std::size_t)},
	{"iteration_no", typeid(std::size_t)},
	{"skips", typeid(std::size_t)},
	{"cpu_time_start", typeid(std::chrono::nanoseconds)},
	{"cpu_time_stop" , typeid(std::chrono::nanoseconds)},
	{"wall_time_start", typeid(std::chrono::high_resolution_clock::time_point)},
	{"wall_time_stop" , typeid(std::chrono::high_resolution_clock::time_point)},
}};

/**
 * @brief A reusable threadloop for plugins.
 *
 * The thread continuously runs `_p_one_iteration()` and is stopable by `stop()`.
 *
 * This factors out the common code I noticed in many different plugins.
 */
class threadloop : public plugin {
public:
	threadloop(std::string name_, phonebook* pb_)
		: plugin{name_, pb_}
		, _m_stoplight{pb->lookup_impl<Stoplight>()}
	{ }

	/**
	 * @brief Starts the thread.
	 */
	virtual void start() override {
		plugin::start();
		_m_thread = std::thread(std::bind(&threadloop::thread_main, this));
		assert(!_m_stoplight->should_stop());
		assert(_m_thread.joinable());
	}

	/**
	 * @brief Joins the thread.
	 *
	 * Must have already stopped the stoplight.
	 */
	virtual void stop() override {
		assert(_m_stoplight->should_stop());
		assert(_m_thread.joinable());
		_m_thread.join();
	}

	virtual ~threadloop() override {
		assert(_m_stoplight->should_stop());
		assert(!_m_thread.joinable());
	}

protected:
	std::size_t iteration_no = 0;
	std::size_t skip_no = 0;

private:

	void thread_main() {
		record_coalescer it_log {record_logger_};
		std::cout << "thread," << std::this_thread::get_id() << ",threadloop," << name << std::endl;

		_p_thread_setup();

		while (!_m_stoplight->should_stop()) {
			_m_stoplight->wait_for_ready();
			skip_option s = _p_should_skip();

			switch (s) {
			case skip_option::skip_and_yield:
				std::this_thread::yield();
				++skip_no;
				break;
			case skip_option::skip_and_spin:
				++skip_no;
				break;
			case skip_option::run: {
				auto iteration_start_cpu_time  = thread_cpu_time();
				auto iteration_start_wall_time = std::chrono::high_resolution_clock::now();
				
				assert(errno == 0);
				_p_one_iteration();
				assert(errno == 0);
				
				it_log.log(record{__threadloop_iteration_header, {
					{id},
					{iteration_no},
					{skip_no},
					{iteration_start_cpu_time},
					{thread_cpu_time()},
					{iteration_start_wall_time},
					{std::chrono::high_resolution_clock::now()},
				}});
				++iteration_no;
				skip_no = 0;
				break;
			}
			case skip_option::stop:
				goto break_loop;
			}
		}
	break_loop:
		[[maybe_unused]] int cpp_requires_a_statement_after_a_label_plz_optimize_me_away;
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

private:
	std::atomic<bool> _m_terminate {false};
	std::thread _m_thread;
	std::shared_ptr<const Stoplight> _m_stoplight;
};

}
