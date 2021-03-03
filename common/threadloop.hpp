#include "plugin.hpp"
#include "cpu_timer.hpp"
#include "managed_thread.hpp"

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
	class threadloop : public plugin, public ManagedThread {
	public:
		threadloop(std::string name_, phonebook* pb_)
			: plugin(name_, pb_)
			, it_log{record_logger_}
		{ }

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

		/*
		  In retrospect, these should be private [1].
		  However, I would have to change ever derived class of threadloop.
		  [1]: http://www.gotw.ca/publications/mill18.htm
		*/

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
			return should_stop();
		}

	private:
		/*
		  In retrospect, I wish I had used the same names as managed_thread here, but alas.
		  I will "rename" methods of threadloop to match managed_thread's names.
		*/
		virtual void on_start() override {
			// logger has to be constructed from the thread of its eventual uses.
			_p_thread_setup();
		}

		size_t skip_no = 0;
		record_coalescer it_log;

		virtual void body() override {
			switch (_p_should_skip()) {
			case skip_option::skip_and_yield:
				sleep(std::chrono::milliseconds{2});
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
					{get_iterations()},
					{skip_no},
					{iteration_start_cpu_time},
					{thread_cpu_time()},
					{iteration_start_wall_time},
					{std::chrono::high_resolution_clock::now()},
				}});
				skip_no = 0;
				break;
			}
			case skip_option::stop:
				request_stop();
				break;
			}
		}
	};
}
