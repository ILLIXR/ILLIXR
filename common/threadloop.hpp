#ifndef THREADLOOP_HH
#define THREADLOOP_HH

#include <atomic>
#include <future>
#include <algorithm>
#include "plugin.hpp"

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
	/**
	 * @brief Starts the thread.
	 */
	void start() override {
		_m_thread = std::thread([this]() {
		while (!should_terminate()) {
				_p_one_iteration();
			}
		});
	}

	/**
	 * @brief Stops the thread.
	 */
	void stop() {
		_m_terminate.store(true);
		_m_thread.join();
	}

	virtual ~threadloop() override {
		if (!should_terminate()) {
			stop();
		}
	}

protected:
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
	void reliable_sleep(std::chrono::time_point<std::chrono::system_clock> stop) {
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

#endif
