#pragma once

#include <atomic>
#include <iostream>
#include <future>
#include <algorithm>
#include "plugin.hpp"
#include "switchboard.hpp"
#include "data_format.hpp"

namespace ILLIXR {

const record_header __threadloop_iteration_header {"threadloop_iteration", {
	{"plugin_id", typeid(std::size_t)},
	{"iteration_no", typeid(std::size_t)},
	{"skips", typeid(std::size_t)},
	{"cpu_time_strat", typeid(std::chrono::nanoseconds)},
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
	threadloop(std::string name_, phonebook* pb_, bool is_scheduled_ = true)
		: plugin(name_, pb_)
		, sb{pb->lookup_impl<switchboard>()}
		, thread_id_publisher{sb->get_writer<thread_info>(std::to_string(id) + "_thread_id")}
		, completion_publisher{sb->get_writer<switchboard::event_wrapper<bool>>(std::to_string(id) + "_completion")}
		, is_scheduled{is_scheduled_}
	{ }

	/**
	 * @brief Starts the thread.
	 */
	virtual void start() override {
		plugin::start();
		if (is_scheduled) {
			[[maybe_unused]] const managed_thread& thread = sb->schedule<switchboard::event_wrapper<bool>>(
				id,
				std::to_string(id) + "_trigger",
				[this](switchboard::ptr<const switchboard::event_wrapper<bool>>, size_t) {
					thread_main(true);
				},
				true
			);
		} else {
			paused.clear();
			assert(id == 7);
			thread = std::make_unique<managed_thread>([this]{
				thread_main(false);
				completion_publisher.put(new (completion_publisher.allocate()) switchboard::event_wrapper<bool> {true});
				},
				[this]{
					char* name = new char[100];
					snprintf(name, 100, "tl_%zu", id);
					name[15] = '\0';
					[[maybe_unused]]int ret = pthread_setname_np(pthread_self(), name);
					assert(!ret);
				},
				[]{},
				cpu_timer::make_type_eraser<FrameInfo>(std::to_string(id))
			);

			thread->start();
			auto pid = thread->get_pid();
			assert(pid != 0);
			{
				std::ofstream file {"self_scheduled_pid"};
				// file.open();
				file << pid << std::endl;;
			}
			thread_id_publisher.put(new (thread_id_publisher.allocate()) thread_info{pid, std::to_string(id)});
		}
	}

	virtual void start2() override {
		if (!is_scheduled) {
			paused.set();
		}
	}

protected:
	std::size_t iteration_no = 0;
	std::size_t skip_no = 0;
	std::unique_ptr<managed_thread> thread {nullptr};

	void thread_main(bool from_switchboard) {
			iteration_no++;
			if (first_time) {
				_p_thread_setup();
				if (!from_switchboard) {
					paused.wait();
				}
				first_time = false;
			}

			skip_option s = _p_should_skip();

			switch (s) {
			case skip_option::skip_and_yield:
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
				// completion_publisher.put(new (completion_publisher.allocate()) switchboard::event_wrapper<bool> {true});
				break;
			}
			case skip_option::stop:
				thread->request_stop();
				break;
			}
		}

private:
	const std::shared_ptr<switchboard> sb;
	switchboard::writer<thread_info> thread_id_publisher;
	switchboard::writer<switchboard::event_wrapper<bool>> completion_publisher;
	bool first_time = true;
	record_coalescer it_log {record_logger_};
	bool is_scheduled;

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

	virtual void stop() override {
		if (!is_scheduled) {
			thread->stop();
			// joins thread here.
			// returns only after thread is dead.
		}
	}

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

private:
	Event paused;
};

}
