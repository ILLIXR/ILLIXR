#pragma once

#include <atomic>
#include <iostream>
#include <future>
#include <algorithm>
#include "plugin.hpp"
#include "switchboard.hpp"
#include "cpu_timer.hpp"
#include "data_format.hpp"

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
		: plugin(name_, pb_)
		, sb{pb->lookup_impl<switchboard>()}
		, thread_id_publisher{sb->get_writer<thread_info>(name_ + "_thread_id")}
		, completion_publisher{sb->get_writer<switchboard::event_wrapper<bool>>(name_ + "_completion")}
	{ }

	/**
	 * @brief Starts the thread.
	 */
	virtual void start() override {
		plugin::start();
		const managed_thread& thread = sb->schedule<switchboard::event_wrapper<bool>>(id, name + "_trigger", [this](switchboard::ptr<const switchboard::event_wrapper<bool>>, size_t) {
			iteration_no++;
			if (first_time) {
				_p_thread_setup();
				first_time = false;
			}

			auto iteration_start_cpu_time  = thread_cpu_time();
			auto iteration_start_wall_time = std::chrono::high_resolution_clock::now();

			skip_option s = _p_should_skip();

			switch (s) {
			case skip_option::skip_and_yield:
				++skip_no;
				break;
			case skip_option::skip_and_spin:
				++skip_no;
				break;
			case skip_option::run: {
				_p_one_iteration();
				it_log.log(record{__threadloop_iteration_header, {
					{id},
					{iteration_no},
					{skip_no},
					{iteration_start_cpu_time},
					{thread_cpu_time()},
					{iteration_start_wall_time},
					{std::chrono::high_resolution_clock::now()},
				}});
				iteration_start_cpu_time  = thread_cpu_time();
				iteration_start_wall_time = std::chrono::high_resolution_clock::now();
				++iteration_no;
				skip_no = 0;
				completion_publisher.put(new (completion_publisher.allocate()) {true});
				break;
			}
			}
		});

		thread_info* info = thread_id_publisher.allocate();
		info->pid = thread.get_pid();
		info->name = name;
		thread_id_publisher.put(info);
		std::cout << "thread," << pid << ",threadloop," << name << std::endl;


	}

protected:
	std::size_t iteration_no = 0;
	std::size_t skip_no = 0;

private:
	const std::shared_ptr<switchboard> sb;
	switchboard::writer<thread_info> thread_id_publisher;
	switchboard::writer<switchboard::event_wrapper<bool>> completion_publisher;
	bool first_time = true;
	record_coalescer it_log {record_logger_};

protected:

	enum class skip_option {
		/// Run iteration NOW. Only then does CPU timer begin counting.
		run,

		/// AKA "busy wait". Skip but try again very quickly.
		skip_and_spin,

		skip_and_yield,
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
};

}
