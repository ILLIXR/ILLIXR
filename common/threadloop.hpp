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
					thread_main();
				},
				true
			);
			// std::cerr << "\e[0;31m";
			// std::cerr << "threadloop::start, is_scheduled, id = " << id << "\n";
			// std::cerr << "\e[0m";
			// thread_id_publisher.put(new (thread_id_publisher.allocate()) thread_info{thread.get_pid(), std::to_string(id)});
		} else {
			thread = std::make_unique<managed_thread>([this]{
				char* name = new char[100];
				snprintf(name, 100, "tl_%zu", id);
				name[15] = '\0';
				[[maybe_unused]]int ret = pthread_setname_np(pthread_self(), name);
				assert(!ret);

				thread_main();
				completion_publisher.put(new (completion_publisher.allocate()) switchboard::event_wrapper<bool> {true});
			});
			thread->start();
			thread->set_cpu(3);
			auto pid = thread->get_pid();
			assert(pid != 0);
			thread_id_publisher.put(new (thread_id_publisher.allocate()) thread_info{pid, std::to_string(id)});
		}
	}

protected:
	std::size_t iteration_no = 0;
	std::size_t skip_no = 0;
	std::unique_ptr<managed_thread> thread {nullptr};

	void thread_main() {
			iteration_no++;
			if (first_time) {
				_p_thread_setup();
				first_time = false;
			}

			// PAUSE

			skip_option s = _p_should_skip();

			switch (s) {
			case skip_option::skip_and_yield:
				++skip_no;
				break;
			case skip_option::skip_and_spin:
				++skip_no;
				break;
			case skip_option::run: {
				auto iteration_start_cpu_time  = thread_cpu_time();
				auto iteration_start_wall_time = std::chrono::high_resolution_clock::now();
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
				++iteration_no;
				skip_no = 0;
				// completion_publisher.put(new (completion_publisher.allocate()) switchboard::event_wrapper<bool> {true});
				break;
			}
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
};

}
