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

	// TODO(sam): perf test: (no annotations, disabled at run-time, coalesced into 1 post-mortem batch but new thread, coalesced into 1 post-mortem batch, enabled coalesced into N batches) x (func call, func call in new thread) without a callback
	// This tells us: disabled at run-time overhead, overhead of func-call logging, overhead of first func-call log in new thread, overhead of batch submission

	using StackFrame = detail::StackFrame;
	using CpuNs = detail::CpuTime;
	using WallNs = detail::WallTime;
	using CallbackType = detail::CallbackType;
	static const auto& make_process = detail::make_process;
	static const auto& get_process = detail::get_process;
	using Process = detail::Process;
	using Stack = detail::Stack;

} // namespace cpu_timer

#ifdef CPU_TIMER_DISABLE
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define CPU_TIMER_TIME_BLOCK_COMMENT(comment, block_name)
#else
#if defined(CPU_TIMER_USE_UNIQUE_PTR)
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define CPU_TIMER_TIME_BLOCK_COMMENT(comment, block_name) const auto CPU_TIMER_DETAIL_TOKENPASTE(cpu_timer_, __LINE__) = cpu_timer::detail::StackFrameContext::create(cpu_timer::detail::tls.get_stack(), comment, block_name, __FILE__, __LINE__);
#else
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define CPU_TIMER_TIME_BLOCK_COMMENT(comment, block_name) const cpu_timer::detail::StackFrameContext CPU_TIMER_DETAIL_TOKENPASTE(cpu_timer_, __LINE__) {cpu_timer::detail::tls.get_stack(), comment, block_name, __FILE__, __LINE__};
#endif
#endif

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define CPU_TIMER_TIME_BLOCK(block_name) CPU_TIMER_TIME_BLOCK_COMMENT("", block_name)

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define CPU_TIMER_TIME_FUNCTION_COMMENT(comment) CPU_TIMER_TIME_BLOCK_COMMENT(comment, __func__)

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define CPU_TIMER_TIME_FUNCTION() CPU_TIMER_TIME_FUNCTION_COMMENT("")
