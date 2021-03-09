#pragma once // NOLINT(llvm-header-guard)
#include "clock.hpp"
#include "compiler_specific.hpp"
#include "type_eraser.hpp"
#include "util.hpp"
#include <cassert>
#include <deque>
#include <functional>
#include <list>
#include <mutex>
#include <ostream>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cpu_timer {
namespace detail {

	static constexpr bool use_fences = true;

	class Frame;
	class Stack;
	class Process;
	class StackFrameContext;

	/**
	 * @brief Timing and runtime data relating to one stack-frame.
	 */
	class Frame {
	private:
		friend class Stack;

		WallTime process_start;

		const char* function_name;
		const char* file_name;
		size_t line;
		// I don't want to use Frame* for pointers to other frames,
		// because they can be moved aroudn in memory (e.g. from stack to finished),
		// and pointers wouldn't work after serialization anyway.
		size_t index;
		size_t caller_index;
		size_t prev_index;
		WallTime start_wall;
		CpuTime start_cpu;
		WallTime stop_wall;
		CpuTime stop_cpu;
		TypeEraser info;

		size_t youngest_child_index;

		void start_timers() {
			assert(start_cpu == CpuTime{0} && "timer already started");

			// very last thing:
			if (use_fences) { fence(); }
			start_wall = wall_now();
			start_cpu = cpu_now();
			if (use_fences) { fence(); }
		}
		void stop_timers() {
			assert(stop_cpu == CpuTime{0} && "timer already started");
			// almost very first thing:
			if (use_fences) { fence(); }
			stop_wall = wall_now();
			stop_cpu = cpu_now();
			if (use_fences) { fence(); }

			assert(start_cpu != CpuTime{0} && "timer never started");
		}

		void start_and_stop_timers(bool wall_time, bool cpu_time) {
			if (use_fences) { fence(); }
			if (wall_time) {
				assert(start_wall == WallTime{0}  && "timer already started");
				assert(start_wall == WallTime{0}  && "timer already stopped" );
				start_wall = stop_wall = wall_now();
			}
			if (cpu_time) {
				start_cpu  = stop_cpu  = cpu_now ();
			}
			if (use_fences) { fence(); }
		}

	public:
		Frame(
			WallTime process_start_,
			const char* function_name_,
			const char* file_name_,
			size_t line_,
			size_t index_,
			size_t caller_index_,
			size_t prev_index_,
			TypeEraser info_
		)
			: process_start{process_start_}
			, function_name{function_name_}
			, file_name{file_name_}
			, line{line_}
			, index{index_}
			, caller_index{caller_index_}
			, prev_index{prev_index_}
			, start_wall{0}
			, start_cpu{0}
			, stop_wall{0}
			, stop_cpu{0}
			, info{std::move(info_)}
			, youngest_child_index{0}
		{ }

		/**
		 * @brief User-specified meaning.
		 */
		const TypeEraser& get_info() const { return info; }
		TypeEraser& get_info() { return info; }

		const char* get_function_name() const { return function_name; }

		const char* get_file_name() const { return file_name; }

		size_t get_line() const { return line; }

		/**
		 * @brief The index of the "parent" Frame (the Frame which called this one).
		 *
		 * The top of the stack is a loop.
		 */
		size_t get_caller_index() const { return caller_index; }

		/**
		 * @brief The index of the "older sibling" Frame (the previous Frame with the same caller).
		 *
		 * 0 if this is the eldest child.
		 */
		size_t get_prev_index() const { return prev_index; }

		bool has_prev() const { return prev_index != 0; }

		/**
		 * @brief See documentation of return type.
		 */
		WallTime get_stop_wall() const { return stop_wall == WallTime{0} ? WallTime{0} : stop_wall - process_start; }

		/**
		 * @brief See documentation of return type.
		 */
		CpuTime get_start_cpu() const { return start_cpu; }

		/**
		 * @brief An index (0..n) according to the order that Frames started (AKA pre-order).
		 */
		size_t get_index() const { return index; }

		/**
		 * @brief See documentation of return type.
		 */
		WallTime get_start_wall() const { return start_wall == WallTime{0} ? WallTime{0} : start_wall - process_start; }

		/**
		 * @brief See documentation of return type.
		 */
		CpuTime get_stop_cpu() const { return stop_cpu; }

		/**
		 * @brief The index of the youngest child (the last direct callee of this frame).
		 */
		size_t get_youngest_callee_index() const { return youngest_child_index; }

		/**
		 * @brief If this Frame calls no other frames
		 */
		bool is_leaf() const { return youngest_child_index == 0; }
	};

	CPU_TIMER_UNUSED static std::ostream& operator<<(std::ostream& os, const Frame& frame) {
		return os
			<< "frame[" << frame.get_index() << "] = "
			<< null_to_empty(frame.get_file_name()) << ":" << frame.get_line() << ":" << null_to_empty(frame.get_function_name())
			<< " called by frame[" << frame.get_caller_index() << "]\n"
			;
	}

	using Frames = std::deque<Frame>;

	class CallbackType {
	protected:
		friend class Stack;
		virtual void thread_start(Stack&) { }
		virtual void thread_in_situ(Stack&) { }
		virtual void thread_stop(Stack&) { }
	public:
		virtual ~CallbackType() { }
	};

	class Stack {
	private:
		friend class Process;
		friend class Frame;
		friend class StackFrameContext;

		Process& process;
		const std::thread::id id;
		const std::thread::native_handle_type native_handle;
		std::string name;
		Frames stack;
		mutable std::mutex finished_mutex;
		Frames finished; // locked by finished_mutex
		size_t index;
		CpuTime last_log;
		const char* blank = "";

		void enter_stack_frame(const char* function_name, const char* file_name, size_t line, TypeEraser info) {
			size_t caller_index = 0;
			size_t prev_index = 0;
			size_t this_index = index++;

			if (CPU_TIMER_LIKELY(!stack.empty())) {
				Frame& caller = stack.back();

				caller_index = caller.index;

				prev_index = caller.youngest_child_index;
				             caller.youngest_child_index = this_index;
			}

			stack.emplace_back(
				get_process_start(),
				function_name,
				file_name,
				line,
				this_index,
				caller_index,
				prev_index,
				std::move(info)
			);

			// very last:
			stack.back().start_timers();
		}

		void exit_stack_frame(CPU_TIMER_UNUSED const char* function_name) {
			assert(!stack.empty() && "somehow exit_stack_frame was called more times than enter_stack_frame");

			// (almost) very first:
			stack.back().stop_timers();

			assert(function_name == stack.back().get_function_name() && "somehow enter_stack_frame and exit_stack_frame for this frame are misaligned");
			{
				// std::lock_guard<std::mutex> finished_lock {finished_mutex};
				finished.emplace_back(std::move(stack.back()));
			}
			stack.pop_back();

			maybe_flush();
		}

	public:

		const Frame& get_top() const { return stack.back(); }
		Frame& get_top() { return stack.back(); }

		void record_event(bool wall_time, bool cpu_time, const char* function_name, const char* file_name, size_t line, TypeEraser info) {
			size_t this_index = index++;

			assert(!stack.empty());
			Frame& caller = stack.back();

			size_t caller_index = caller.index;

			size_t prev_index = caller.youngest_child_index;
			                    caller.youngest_child_index = this_index;
			
			
			finished.emplace_back(
				get_process_start(),
				function_name,
				file_name,
				line,
				this_index,
				caller_index,
				prev_index,
				std::move(info)
			);
			finished.back().start_and_stop_timers(wall_time, cpu_time);

			maybe_flush();
		}

		Stack(Process& process_, std::thread::id id_, std::thread::native_handle_type native_handle_, std::string&& name_)
			: process{process_}
			, id{id_}
			, native_handle{native_handle_}
			, name{std::move(name_)}
			, index{0}
			, last_log{0}
		{
			enter_stack_frame(blank, blank, 0, type_eraser_default);
			get_callback().thread_start(*this);
		}

		~Stack() {
			exit_stack_frame(blank);
			assert(stack.empty() && "somewhow enter_stack_frame was called more times than exit_stack_frame");
			get_callback().thread_stop(*this);
			assert(finished.empty() && "flush() should drain this buffer, and nobody should be adding to it now. Somehow unflushed Frames are still present");
		}

		// I do stuff in the destructor that should only happen once per constructor-call.
		Stack(const Stack& other) = delete;
		Stack& operator=(const Stack& other) = delete;

		Stack(Stack&& other) noexcept
			: process{other.process}
			, id{other.id}
			, native_handle{other.native_handle}
			, name{std::move(other.name)}
			, stack{std::move(other.stack)}
			, finished{std::move(other.finished)}
			, index{other.index}
			, last_log{other.last_log}
		{ }
		Stack& operator=(Stack&& other) = delete;

		std::thread::id get_id() const { return id; }

		std::thread::native_handle_type get_native_handle() const { return native_handle; }

		std::string get_name() const { return name; }

		void set_name(std::string&& name_) { name = std::move(name_); }

		const Frames& get_stack() const { return stack; }

		Frames drain_finished() {
			Frames finished_buffer;
			finished.swap(finished_buffer);
			return finished_buffer;
		}

	private:
		void maybe_flush() {
			std::lock_guard<std::mutex> finished_lock {finished_mutex};
			// get CPU time is expensive. Instead we look at the last frame
			if (!finished.empty()) {
				CpuTime now = finished.back().get_stop_cpu();

				// std::lock_guard<std::mutex> config_lock {process.config_mutex};
				CpuTime process_log_period = get_log_period();

				if (get_ns(process_log_period) != 0 && (get_ns(process_log_period) == 1 || now > last_log + process_log_period)) {
					get_callback().thread_in_situ(*this);
				}
			}
		}

		CallbackType& get_callback() const;
		CpuTime get_log_period() const;
		WallTime get_process_start() const;
	};

	/**
	 * @brief All stacks in the current process.
	 *
	 * This calls callback with one thread's batches of Frames, periodically not sooner than log_period, in the thread whose functions are in the batch.
	 */
	class Process {
	private:
		friend class Stack;
		friend class StackFrameContext;

		// std::atomic<bool> enabled;
		bool enabled;
		// std::mutex config_mutex;
		const WallTime start;
		CpuTime log_period; // locked by config_mutex
		std::unique_ptr<CallbackType> callback; // locked by config_mutex
		// Actually, I don't need to lock the config
		// If two threads race to modify the config, the "winner" is already non-deterministic
		// The callers should synchronize themselves.
		// If this thread writes while someone else reads, there is no guarantee they hadn't "already read" the values.
		// The caller should synchronize with the readers.
		std::unordered_map<std::thread::id, Stack> thread_to_stack; // locked by thread_to_stack_mutex
		std::unordered_map<std::thread::id, size_t> thread_use_count; // locked by thread_to_stack_mutex
		mutable std::recursive_mutex thread_to_stack_mutex;

	public:

		explicit Process()
			: enabled{false}
			, start{wall_now()}
			, log_period{0}
			, callback{std::make_unique<CallbackType>()}
		{ }

		WallTime get_start() { return start; }

		/**
		 * @brief Create or get the stack.
		 *
		 * For efficiency, the caller should cache this in thread_local storage.
		 */
		Stack& create_stack(
				std::thread::id thread,
				std::thread::native_handle_type native_handle,
				std::string&& thread_name
		) {
			std::lock_guard<std::recursive_mutex> thread_to_stack_lock {thread_to_stack_mutex};
			// This could be the same thread, just a different static context (i.e. different obj-file or lib)
			// Could have also been set up by the caller.
			if (thread_to_stack.count(thread) == 0) {
				thread_to_stack.emplace(
					std::piecewise_construct,
					std::forward_as_tuple(thread),
					std::forward_as_tuple(*this, thread, native_handle, std::move(thread_name))
				);
			}
			thread_use_count[thread]++;
			return thread_to_stack.at(thread);
		}

		/**
		 * @brief Call when a thread is disposed.
		 *
		 * This is neccessary because the OS can reuse old thread IDs.
		 */
		void delete_stack(std::thread::id thread) {
			std::lock_guard<std::recursive_mutex> thread_to_stack_lock {thread_to_stack_mutex};
			// This could be the same thread, just a different static context (i.e. different obj-file or lib)
			if (thread_to_stack.count(thread) != 0) {
				thread_use_count[thread]--;
				if (thread_use_count.at(thread) == 0) {
					thread_to_stack.erase(thread);
				}
			}
		}

		/**
		 * @brief Sets @p enabled for future threads.
		 *
		 * All in-progress threads will complete with the prior value.
		 */
		void set_enabled(bool enabled_) {
			enabled = enabled_;
		}

		/**
		 * @brief Sets @p log_period for future threads.
		 *
		 * All in-progress threads will complete with the prior value.
		 */
		void set_log_period(CpuTime log_period_) {
			// std::lock_guard<std::recursive_mutex> config_lock {config_mutex};
			log_period = log_period_;
		}

		bool is_enabled() {
			return enabled;
		}

		/**
		 * @brief Sets @p callback for future threads.
		 *
		 * All in-progress threads will complete with the prior value.
		 */
		void set_callback(std::unique_ptr<CallbackType>&& callback_) {
			callback = std::move(callback_);
		}

		// get_stack() returns pointers into this, so it should not be copied or moved.
		Process(const Process&) = delete;
		Process& operator=(const Process&) = delete;
		Process(const Process&&) = delete;
		Process& operator=(const Process&&) = delete;
		~Process() {
			for (const auto& pair : thread_to_stack) {
				std::cerr << pair.first << " is still around. Going to kick their logs out.\n";
			}
		}
	};

	/**
	 * @brief An RAII context for creating, stopping, and storing Frames.
	 */
	class StackFrameContext {
	private:
		Process& process;
		Stack& stack;
		const char* function_name;
		bool enabled;
	public:
		/**
		 * @brief Begins a new RAII context for a StackFrame in Stack, if enabled.
		 */
		StackFrameContext(Process& process_, Stack& stack_, const char* function_name_, const char* file_name, size_t line, TypeEraser info)
			: process{process_}
			, stack{stack_}
			, function_name{function_name_}
			, enabled{process.is_enabled()}
		{
			if (enabled) {
				stack.enter_stack_frame(function_name, file_name, line, std::move(info));
			}
		}

		/*
		 * I have a custom destructor, and should there be a copy, the destructor will run twice, and double-count the frame.
		 * Therefore, I disallow copies.
		 */
		StackFrameContext(const StackFrameContext&) = delete;
		StackFrameContext& operator=(const StackFrameContext&) = delete;

		// I could support this, but I don't think I need to.
		StackFrameContext(StackFrameContext&&) = delete;
		StackFrameContext& operator=(StackFrameContext&&) = delete;

		/**
		 * @brief Completes the StackFrame in Stack.
		 */
		~StackFrameContext() {
			if (enabled) {
				stack.exit_stack_frame(function_name);
			}
		}
	};

	inline CallbackType& Stack::get_callback() const { return *process.callback; }

	inline CpuTime Stack::get_log_period() const { return process.log_period; }

	inline WallTime Stack::get_process_start() const { return process.start; }

	// TODO(grayson5): Figure out which threads the callback could be called from.
	// thread_in_situ will always be called from the target thread.
	// I believe thread_local StackContainer call create_stack and delete_stack, so I think they will always be called from the target thread.
	// But the main thread might get deleted by the destructor of its containing map, in the main thread.

} // namespace detail
} // namespace cpu_timer
