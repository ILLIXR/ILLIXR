#ifndef THREADLOOP_HH
#define THREADLOOP_HH

#include <atomic>
#include <future>
#include <algorithm>
#include "plugin.hh"

namespace ILLIXR {

class threadloop : public plugin {
public:
	void start() override {
		_m_thread = std::thread([this]() {
		while (!should_terminate()) {
				_p_one_iteration();
			}
		});
	}

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
	virtual void _p_one_iteration() = 0;

	bool should_terminate() {
		return _m_terminate.load();
	}

	template< class Rep, class Period >
	void reliable_sleep(const std::chrono::duration<Rep, Period>& sleep_duration) {
		auto start = std::chrono::high_resolution_clock::now();
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
