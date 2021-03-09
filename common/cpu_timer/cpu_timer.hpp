#pragma once // NOLINT(llvm-header-guard)

/**
 * @brief This is the cpu_timer.
 *
 * Usage:
 *
 * 1. Call `cpu_timer::make_process(...)` exactly once.
 *
 * 2. Time your functions with `CPU_TIMER_TIME_FUNCTION()` and time your
 * blocks with `CPU_TIMER_TIME_BLOCKS("block name")`.
 *
 * 3. `#define CPU_TIMER_DISABLE` to disable it at compiletime. All of
 *    your calls to cpu_timer should compile away to noops. This
 *    allows you to measure the overhead of the cpu_timer itself.
 *
 * Motivation:
 *
 * - It is "opt-in" timing. Nothing is timed, except what you ask
 *   for. This is in contrast to `-pg`.
 *
 * - It uses RAII, so just have to add one line time a block.
 *
 * - Each timing record maintains a record to the most recent caller
 *   that opted-in to timing. The timer is "context-sensitive".
 *
 * - Each timing record can optionally contain arguments or runtime
 *   information.
 *
 * - I report both a wall clock (real time since program startup) and
 *   CPU time spent on that thread. Both of these should be monotonic.
 *
 * - These timers have a ~400ns overhead (check clocks + storing frame
 *   overhead) per frame timed on my system. Run ./test.sh to check on
 *   yours.
 *
 * - I use clock_gettime with CLOCK_THREAD_CPUTIME_ID (cpu time) and
 *   CLOCK_MONOTONIC (wall time). rdtsc won't track CPU time if the
 *   thread gets interrupted [2], and I "need" the extra work
 *   that clock_gettime(CLOCK_MONOTIC) does to convert tsc into a wall
 *   time. The VDSO interface mitigates sycall overhead. In some
 *   cases, clock_gettime is faster [1].
 *
 * [1]: https://stackoverflow.com/questions/7935518/is-clock-gettime-adequate-for-submicrosecond-timing
 * [2]: https://stackoverflow.com/questions/42189976/calculate-system-time-using-rdtsc
 *
 */

#include "cpu_timer_internal.hpp"
#include "global_state.hpp"
namespace cpu_timer {

	using Frames = detail::Frames;
	using Frame = detail::Frame;
	using CpuNs = detail::CpuTime;
	using WallNs = detail::WallTime;
	using CallbackType = detail::CallbackType;
	using Process = detail::Process;
	using Stack = detail::Stack;
	using TypeEraser = detail::TypeEraser;

	// Function aliases https://www.fluentcpp.com/2017/10/27/function-aliases-cpp/
	// Whoops, we don't use this anymore, bc we need to bind methods to their static object.

	CPU_TIMER_UNUSED static Process& get_process() { return detail::process_container.get_process(); }

	CPU_TIMER_UNUSED static Stack& get_stack() { return detail::stack_container.get_stack(); }

	static constexpr auto& type_eraser_default = cpu_timer::detail::type_eraser_default;

	// In C++14, we could use templated function aliases
	template <typename T>
	TypeEraser make_type_eraser(T* ptr) {
		return std::static_pointer_cast<void>(std::shared_ptr<T>{ptr});
	}

	template <typename T, class... Args>
	TypeEraser make_type_eraser(Args&&... args) {
		return std::static_pointer_cast<void>(std::make_shared<T>(std::forward<Args>(args)...));
	}

	template <typename T>
	const T& extract_type_eraser(const TypeEraser& type_eraser) {
		return *std::static_pointer_cast<T>(type_eraser);
	}

	template <typename T>
	T& extract_type_eraser(TypeEraser& type_eraser) {
		return *std::static_pointer_cast<T>(type_eraser);
	}

} // namespace cpu_timer

#if defined(CPU_TIMER_DISABLE)
#define CPU_TIMER_TIME_BLOCK_INFO(block_name, info)
#define CPU_TIMER_TIME_EVENT_INFO(event_name, info)
#else

#define CPU_TIMER_TIME_BLOCK_INFO(block_name, info) \
	const cpu_timer::detail::StackFrameContext CPU_TIMER_DETAIL_TOKENPASTE(cpu_timer_, __LINE__) {\
		cpu_timer::get_process(), cpu_timer::get_stack(), block_name, __FILE__, __LINE__, info \
	};

#define CPU_TIMER_TIME_EVENT_INFO(wall_time, cpu_time, event_name, info) \
	{ \
		cpu_timer::get_stack().record_event(wall_time, cpu_time, event_name, __FILE__, __LINE__, info); \
	}
#endif

#define CPU_TIMER_TIME_BLOCK(block_name) CPU_TIMER_TIME_BLOCK_INFO(block_name, cpu_timer::type_eraser_default)

#define CPU_TIMER_TIME_FUNCTION_INFO(info) CPU_TIMER_TIME_BLOCK_INFO(__func__, info)

#define CPU_TIMER_TIME_FUNCTION() CPU_TIMER_TIME_FUNCTION_INFO(cpu_timer::type_eraser_default)

#define CPU_TIMER_TIME_EVENT() CPU_TIMER_TIME_EVENT_INFO(false, false, __func__, cpu_timer::type_eraser_default)
