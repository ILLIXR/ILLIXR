#include <unistd.h>
#include <string.h>

namespace ILLIXR {
	[[maybe_unused]] static bool is_default_scheduler() {
		static const char* ILLIXR_SCHEDULER_str = std::getenv("ILLIXR_SCHEDULER");
		static bool ret = ILLIXR_SCHEDULER_str && (strcmp(ILLIXR_SCHEDULER_str, "default") == 0);
		return ret;
	}

	[[maybe_unused]] static bool is_priority_scheduler() {
		static const char* ILLIXR_SCHEDULER_str = std::getenv("ILLIXR_SCHEDULER");
		static bool ret = ILLIXR_SCHEDULER_str && (strcmp(ILLIXR_SCHEDULER_str, "priority") == 0);
		return ret;
	}

	[[maybe_unused]] static bool is_manual_scheduler() {
		static const char* ILLIXR_SCHEDULER_str = std::getenv("ILLIXR_SCHEDULER");
		static bool ret = ILLIXR_SCHEDULER_str && (strcmp(ILLIXR_SCHEDULER_str, "manual") == 0);
		return ret;
	}

	[[maybe_unused]] static bool is_static_scheduler() {
		static const char* ILLIXR_SCHEDULER_str = std::getenv("ILLIXR_SCHEDULER");
		static bool ret = ILLIXR_SCHEDULER_str && (strcmp(ILLIXR_SCHEDULER_str, "static") == 0);
		return ret;
	}

	[[maybe_unused]] static bool is_dynamic_scheduler() {
		static const char* ILLIXR_SCHEDULER_str = std::getenv("ILLIXR_SCHEDULER");
		static bool ret = ILLIXR_SCHEDULER_str && (strcmp(ILLIXR_SCHEDULER_str, "dynamic") == 0);
		return ret;
	}
}
