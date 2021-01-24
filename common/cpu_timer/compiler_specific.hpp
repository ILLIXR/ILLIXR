#pragma once // NOLINT(llvm-header-guard)
#if defined(__clang__) || defined(__GNUC__)
#define CPU_TIMER_LIKELY(x)      __builtin_expect(!!(x), 1)
#define CPU_TIMER_UNLIKELY(x)    __builtin_expect(!!(x), 0)
#define CPU_TIMER_UNUSED              [[maybe_unused]]
#else
#define CPU_TIMER_LIKELY(x)      x
#define CPU_TIMER_UNLIKELY(x)    x
#define CPU_TIMER_UNUSED
#endif
