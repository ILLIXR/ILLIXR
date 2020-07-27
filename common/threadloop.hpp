#pragma once

#include <atomic>
#include <iostream>
#include <future>
#include <algorithm>
#include "plugin.hpp"
#include "cpu_timer.hpp"

namespace ILLIXR {

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
		_m_thread = std::thread(std::bind(&threadloop::thread_main, this));
	}

	/**
	 * @brief Stops the thread.
	 */
	virtual void stop() override {
		_m_terminate.store(true);
		_m_thread.join();
	}

	virtual ~threadloop() override {
		if (!should_terminate()) {
			stop();
		}
	}

private:
	void thread_main() {
		metric_coalescer<start_iteration_record> start_it {metric_logger};
		metric_coalescer<stop_iteration_record> stop_it {metric_logger};
		metric_coalescer<start_skip_iteration_record> start_skip {metric_logger};
		metric_coalescer<stop_skip_iteration_record> stop_skip {metric_logger};

		std::size_t it = 0;
		std::size_t skip_it = 0;

		_p_thread_setup();

		while (!should_terminate()) {

			start_skip.log(std::make_unique<const start_skip_iteration_record>(id, it, skip_it));
			skip_option s = _p_should_skip();
			stop_skip.log(std::make_unique<const stop_skip_iteration_record>(id, it, skip_it));

			switch (s) {
			case skip_option::skip_and_yield:
				std::this_thread::yield();
				++skip_it;
				break;
			case skip_option::skip_and_spin:
				++skip_it;
				break;
			case skip_option::run:
				start_it.log(std::make_unique<const start_iteration_record>(id, it, skip_it));
				_p_one_iteration();
				stop_it.log(std::make_unique<const stop_iteration_record>(id, it, skip_it));
				++it;
				skip_it = 0;
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
		auto start = std::chrono::high_resolution_clock::now();
		auto sleep_duration = stop - start;

		auto sleep_quantum = std::min<std::common_type_t<decltype(sleep_duration), decltype(MAX_TIMEOUT)>>(
			sleep_duration / SLEEP_SAFETY_FACTOR,
			MAX_TIMEOUT
		);

		// sleep_quantum is at most MAX_TIMEOUT so that we will wake up, and check if should_terminate
		// Thus, every plugin will respond to termination within MAX_TIMOUT (assuming no long compute-bound thing)
		while (!should_terminate() && std::chrono::high_resolution_clock::now() - start < sleep_duration) {
			std::this_thread::sleep_for(sleep_quantum);
		}
	}

private:
	// This factor is related to how accurate reliable_sleep is
	const size_t SLEEP_SAFETY_FACTOR {100};

	// this factor is related to how quickly we will shutdown when termintae is called
	std::chrono::milliseconds MAX_TIMEOUT {100};

	std::atomic<bool> _m_terminate {false};

	std::thread _m_thread;
};

}
