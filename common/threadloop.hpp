#pragma once

#include <atomic>
#include <iostream>
#include <future>
#include <algorithm>
#include "plugin.hpp"
#include "managed_thread.hpp"
#include "cpu_timer/cpu_timer.hpp"

namespace ILLIXR {

	class threadloop_marker {
	public:
		plugin_id_t plugin_id;
		threadloop_marker(plugin_id_t plugin_id_)
			: plugin_id{plugin_id_}
		{ }
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
	threadloop(std::string name_, phonebook* pb_)
		: plugin{name_, pb_}
		, _m_thread{
			[this]{this->thread_main();},
			[this]{this->thread_setup();},
			nullptr,
			cpu_timer::make_type_eraser<threadloop_marker>(id)
		}
	{ }

	/**
	 * @brief Starts the thread.
	 */
	virtual void start() override {
		plugin::start();
		_m_thread.start();
	}

	/**
	 * @brief Stops the thread.
	 */
	virtual void stop() override {
		_m_thread.stop();
		plugin::stop();
	}

protected:
	std::size_t iteration_no = 0;
	std::size_t skip_no = 0;

private:

	void thread_setup() {
		_p_thread_setup();
	}

	void thread_main() {
		skip_option s;
		{
			CPU_TIMER_TIME_BLOCK("_p_should_skip");
			s = _p_should_skip();
		}

		switch (s) {
		case skip_option::skip_and_yield:
			std::this_thread::yield();
			++skip_no;
			break;
		case skip_option::skip_and_spin:
			++skip_no;
			break;
		case skip_option::run: {
			CPU_TIMER_TIME_BLOCK("_p_one_iteration");
			_p_one_iteration();
			++iteration_no;
			skip_no = 0;
			break;
		}
		case skip_option::stop:
			stop();
			break;
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

private:
	std::atomic<bool> _m_terminate {false};

	managed_thread _m_thread;
};

}
