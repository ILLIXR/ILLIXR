/**
 * @brief Unit tests for cpu_timer (include/illixr/cpu_timer.hpp).
 */

#include "illixr/cpu_timer.hpp"

#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

namespace ILLIXR {

namespace {

TEST(CpuTimerTest, ThreadCpuTimeIsNonNegative) {
    ASSERT_GE(thread_cpu_time().count(), 0);
}

TEST(CpuTimerTest, ThreadCpuTimeIncreasesUnderLoad) {
    auto          before = thread_cpu_time();
    volatile long sum    = 0;
    for (long i = 0; i < 10'000'000; ++i) {
        sum += i;
    }
    auto after = thread_cpu_time();
    ASSERT_GT(after.count(), before.count());
}

TEST(CpuTimerTest, TimerMeasuresElapsedTicksFromSyntheticClock) {
    // A synthetic "clock" (a plain counter) makes this deterministic instead of depending on
    // real elapsed time.
    int  tick         = 0;
    auto synthetic_now = [&tick]() {
      return tick;
    };

    int duration = 0;
    {
        timer<decltype(synthetic_now)> t{synthetic_now, duration};
        tick += 5;
    }
    ASSERT_EQ(duration, 5);
}

TEST(CpuTimerTest, CountDurationConvertsToNanoseconds) {
    ASSERT_EQ(count_duration(std::chrono::milliseconds{1}), 1'000'000);
    ASSERT_EQ(count_duration(std::chrono::seconds{2}), 2'000'000'000);
}

TEST(CpuTimerTest, CountDurationPassesThroughIntegralTypes) {
    ASSERT_EQ(count_duration(42), 42);
}

TEST(CpuTimerTest, TimedThreadRunsAndJoinsTheGivenFunction) {
    std::atomic<bool> ran{false};
    std::thread       t = timed_thread("test_account", [&ran]() {
      ran = true;
    });
    t.join();
    ASSERT_TRUE(ran.load());
}

} // namespace

} // namespace ILLIXR
