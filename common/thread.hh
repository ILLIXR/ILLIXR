#ifndef ABSTRACT_COMPONENT_HH
#define ABSTRACT_COMPONENT_HH

#include <atomic>
#include <future>
#include "phonebook.hh"

namespace ILLIXR {

class threadloop : public destructible {
public:
	void start() {
		_m_thread = std::thread([this]() {
			while (should_terminate) {
				_p_one_iteration();
			}
		});
	}

	void stop() {
		_m_terminate.store(true);
		_m_thread.join();
	}

	~threadloop() {
		if (!_m_terminate.load()) {
			stop();
		}
	}

protected:
	void _p_one_iteration() {}

	bool should_terminate() {
		return !_m_terminate.load();
	}

	template< class Rep, class Period >
	static void reliable_sleep(const std::chrono::duration<Rep, Period>& sleep_duration) {
		auto start = std::chrono::high_resolution_clock::now();
		auto sleep_quantum = std::chrono::duration<Rep, Period>::min(sleep_duration / TIME_SAFETY_FACTOR);
		while (should_terminate() && std::chrono::high_resolution_clock::now() - start < sleep_duration) {
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
