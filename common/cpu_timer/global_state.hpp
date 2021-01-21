#pragma once // NOLINT(llvm-header-guard)
#include "compiler_specific.hpp"
#include "cpu_timer_internal.hpp"
#include "os_specific.hpp"
#include "util.hpp"
#include <memory>
#include <fstream>
#include <string>
#include <thread>

namespace cpu_timer {
namespace detail {

	/*

	  I want all funcs in this translation-unit to share the same
	  process. Therefore, it should be static.

	  I want all translation-units to share the same process,
	  therefore it must be a pointer.

	  I could have no individual client responsible for creating the
	  process; the first one which needs it would create it. But I
	  decided instead to bless one client with the responsibility of
	  making the process, while the others just look it up.

	  Cons:

	  - One lib has to be "special".

	  Pros:

	  - It has a single site to specify "initial configuration,"
	  without which, I would have to construct the Process in a
	  half-initialized state.

	  - No race on the fs to publish `make_process` tempfile.

	  However, the lib responsible for making won't necessarily be
	  loaded first or unloaded last. In such a case, hence each client
	  needs to hold a std::shared_ptr<...>.

	  It also needs to be lazily-loaded (not initailized at
	  load-time), because it is possible nobody has called
	  `make_process` yet. `get_process()` can still use
	  branch-prediction to make it fast in the steady-state despite
	  lazy-initialization (having to check if it is initialized).

	*/

	static std::shared_ptr<Process> process;

	static std::string get_filename() {
		return tmp_path(std::to_string(get_pid()));
	}

	static void make_process(bool is_enabled, CpuTime log_period, CallbackType callback) {
		process = std::make_shared<Process>(is_enabled, log_period, callback);
		std::ofstream outfile {get_filename()};
		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
		uintptr_t intptr = reinterpret_cast<uintptr_t>(&process);
		outfile << intptr;
	}

	static std::shared_ptr<Process> lookup_process() {
		std::ifstream infile {get_filename()};
		if (infile.good()) {
			uintptr_t intptr = 0;
			infile >> intptr;
			assert(intptr);
			// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
			return *reinterpret_cast<std::shared_ptr<Process>*>(intptr);
		}
		error("Must call make_process(...) in the main thread before any other cpu_timer actions.");
		return std::shared_ptr<Process>();
	}

	static Process& get_process() {
		if (CPU_TIMER_UNLIKELY(!process)) {
			process = lookup_process();
		}
		return *process;
	}

	/*
	  I want the Stack to be in thread-local storage, so each thread
	  can cheaply access their own (cheaper than looking up in a map
	  from thread::id -> Stack, I think).

	  However, I can't just `static thread_local Stack`, because the
	  parameters for creation depend on the process. Therefore I will
	  `static thread_local OBJECT`, where the sole responsibility is
	  to construct and hold a Stack.
	*/

	class ThreadLocalStack {
		Process& process;
		Stack& stack;
	public:
		ThreadLocalStack()
			: process{get_process()}
			, stack{process.create_stack(std::this_thread::get_id(), get_thread_name())}
		{ }
		~ThreadLocalStack() {
			process.remove_stack(std::this_thread::get_id());
		}
		Stack& get_stack() { return stack; }
	};
	static thread_local ThreadLocalStack tls;

} // namespace detail
} // namespace cpu_timer
