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
	  I want to hold a process with a static lifetime.
	  I don't want anyone to access it directly.
	  This gives me the possibility of lazy-loading.
	  Therefore, I will construct this at load-time, and call get_process at use-time.
	 */
	static class ProcessContainer {
	private:
		std::string filename;
		std::shared_ptr<Process> process;

		/*
		  This may need to lookup the address of the process,
		  so it can't return a shared_ptr; it has to mutate the container.
		 */
		void create_or_lookup_process() {
			std::ifstream infile {filename};
			if (CPU_TIMER_LIKELY(infile.good())) {
				uintptr_t intptr = 0;
				infile >> intptr;
				// std::cerr << "ProcessContainer::create_or_lookup_process() lookup got " << reinterpret_cast<void*>(intptr) << " from " << filename << "\n";
				assert(intptr);
				// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
				process = *reinterpret_cast<std::shared_ptr<Process>*>(intptr);
			} else {
				infile.close();
				process = std::make_shared<Process>();
				std::ofstream outfile {filename};
				// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
				uintptr_t intptr = reinterpret_cast<uintptr_t>(&process);
				// std::cerr << "ProcessContainer::create_or_lookup_process() create put " << reinterpret_cast<void*>(intptr) << " into " << filename << "\n";
				outfile << intptr;
			}
		}

	public:
		ProcessContainer()
			: filename{tmp_path(std::to_string(get_pid()) + "_" + std::to_string(get_pid_uniquifier()))}
		{
			// std::cerr << "ProcessContainer::ProcessContainer()\n";
		}
		~ProcessContainer() {
			// std::cerr << "ProcessContainer::~ProcessContainer()\n";
			if (process.unique()) {
				[[maybe_unused]] int rc = std::remove(filename.c_str());
				assert(rc == 0);
			}
		}
		Process& get_process() {
			if (CPU_TIMER_UNLIKELY(!process)) {
				create_or_lookup_process();
			}
			return *process;
		}
	} process_container;

	/*
	  I want the Stack to be in thread-local storage, so each thread
	  can cheaply access their own (cheaper than looking up in a map
	  from thread::id -> Stack, I think).

	  However, I can't just `static thread_local Stack`, because the
	  parameters for creation depend on the process. Therefore I will
	  `static thread_local OBJECT`, where the sole responsibility is
	  to construct and hold a Stack.
	*/
	static thread_local class StackContainer {
	private:
		std::thread::id id;
		Process& process;
		Stack& stack;

	public:
		StackContainer()
			: id{std::this_thread::get_id()}
			, process{process_container.get_process()}
			, stack{process.create_stack(id, get_tid(), get_thread_name())}
		{ }

		~StackContainer() {
			process.delete_stack(id);
		}

		Stack& get_stack() { return stack; }
	} stack_container;

} // namespace detail
} // namespace cpu_timer
