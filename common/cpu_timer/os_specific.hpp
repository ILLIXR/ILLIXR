#pragma once // NOLINT(llvm-header-guard)

#include "compiler_specific.hpp"

#if defined (__unix__)

#include <cerrno>
#include <unistd.h>
// #include <sys/types.h>
#include <pthread.h>

namespace cpu_timer {
namespace detail {

	using ProcessId = size_t;
	static ProcessId get_pid() {
		return ::getpid();
	}

	// using ThreadId = size_t;
	// static ThreadId get_tid() {
	// 	return gettid();
	// }

	static std::string tmp_path(std::string data) {
		return "/tmp/cpu_timer_" + data;
	}

	static std::string get_thread_name() {
		constexpr size_t NAMELEN = 16;
		char thread_name_buffer[NAMELEN];
		int rc = pthread_getname_np(pthread_self(), thread_name_buffer, NAMELEN);
		if (CPU_TIMER_UNLIKELY(rc)) {
			return std::string{};
		}
		return std::string{thread_name_buffer};
	}

} // namespace detail
} // namespace cpu_timer

#else

#error "os_specicic.hpp is not supported for your platform"

#endif
