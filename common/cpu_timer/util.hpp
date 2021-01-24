#pragma once // NOLINT(llvm-header-guard)
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <random>
#include <string>

#define CPU_TIMER_DETAIL_TOKENPASTE_(x, y) x ## y
#define CPU_TIMER_DETAIL_TOKENPASTE(x, y) CPU_TIMER_DETAIL_TOKENPASTE_(x, y)

namespace cpu_timer {
namespace detail {

	/**
	 * @brief if var is env-var return it, else default_
	 */
	// static std::string
	// getenv_or(const std::string& var, std::string default_) {
	// 	if (std::getenv(var.c_str()) != nullptr) {
	// 		return {std::getenv(var.c_str())};
	// 	} else {
	// 		return default_;
	// 	}
	// }

	static void fence() {
		std::atomic_thread_fence(std::memory_order_seq_cst);
	}

	template <typename Map, typename RevMap>
	typename Map::mapped_type lookup(Map& map, RevMap& reverse_map, typename Map::key_type word) {
		auto it = map.find(word);
		if (it != map.end()) {
			assert(reverse_map[it->second] == it->first);
			return it->second;
		} else {
			auto val = map.size();
			map[word] = val;
			reverse_map.push_back(word);
			return val;
		}
	}

	// static void error(const char* msg) {
	// 	std::cerr << msg << "\n";
	// 	abort();
	// }

	static const char* null_to_empty(const char* str) {
		static constexpr const char* empty = "";
		if (str) {
			return str;
		} else {
			return empty;
		}
	}

} // namespace detail
} // namespace cpu_timer
