#pragma once // NOLINT(llvm-header-guard)

#include "compiler_specific.hpp"

#if defined (__linux__)

#include <cerrno>
#include <fstream>
#include <pthread.h>
#include <string>
#include <sys/syscall.h>
#include <unistd.h>

namespace cpu_timer {
namespace detail {

	using ProcessId = size_t;
	static ProcessId get_pid() {
		return ::getpid();
	}

	/*
	 * @brief A unique number for each process
	 *
	 * This is necessary because pids may be reused.
	 */
	static size_t get_pid_uniquifier() {
		// See /proc/[pid]/stat
		// https://man7.org/linux/man-pages/man5/proc.5.html
		// field 21: STARTTIME
		static constexpr size_t STARTTIME_FIELD = 22;
		std::ifstream stat_file {"/proc/self/stat"};
		std::string field;
		for (size_t i = 0; i < STARTTIME_FIELD; ++i) {
			std::getline(stat_file, field, ' ');
		}
		return std::stoi(field);
	}

	using ThreadId = size_t;
	static ThreadId get_tid() {
		return ::syscall(SYS_gettid);
	}

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
