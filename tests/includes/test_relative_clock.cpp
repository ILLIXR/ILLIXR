/**
 * @brief Unit tests for relative_clock (include/illixr/relative_clock.hpp).
 */

#include "illixr/relative_clock.hpp"

#include <gtest/gtest.h>
#include <thread>

namespace ILLIXR {

namespace {

TEST(TimePointTest, DefaultConstructedIsZero) {
    time_point tp;
    ASSERT_EQ(tp.time_since_epoch().count(), 0);
}

TEST(TimePointTest, SubtractionGivesDuration) {
    time_point a{time_point::duration{100}};
    time_point b{time_point::duration{40}};
    ASSERT_EQ((a - b).count(), 60);
}

TEST(TimePointTest, AdditionOperators) {
    time_point           a{time_point::duration{10}};
    time_point::duration d{5};
    ASSERT_EQ((a + d).time_since_epoch().count(), 15);
    ASSERT_EQ((d + a).time_since_epoch().count(), 15);
}

TEST(TimePointTest, ComparisonOperators) {
    time_point a{time_point::duration{10}};
    time_point b{time_point::duration{20}};
    ASSERT_TRUE(a < b);
    ASSERT_TRUE(b > a);
    ASSERT_TRUE(a <= a);
    ASSERT_TRUE(a >= a);
    ASSERT_TRUE(a == a);
    ASSERT_TRUE(a != b);
}

TEST(TimePointTest, PlusEqualsAndMinusEquals) {
    time_point tp{time_point::duration{10}};
    tp += time_point::duration{5};
    ASSERT_EQ(tp.time_since_epoch().count(), 15);
    tp -= time_point::duration{3};
    ASSERT_EQ(tp.time_since_epoch().count(), 12);
}

TEST(RelativeClockTest, IsNotStartedBeforeStart) {
    relative_clock clock;
    ASSERT_FALSE(clock.is_started());
}

TEST(RelativeClockTest, IsStartedAfterStart) {
    relative_clock clock;
    clock.start();
    ASSERT_TRUE(clock.is_started());
}

TEST(RelativeClockTest, NowAdvancesAfterStart) {
    relative_clock clock;
    clock.start();
    time_point first = clock.now();
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
    time_point second = clock.now();
    ASSERT_GT(second, first);
}

TEST(RelativeClockTest, NowBeforeStartIsAnAssertionFailure) {
    relative_clock clock;
#ifndef NDEBUG
    ASSERT_DEATH({ clock.now(); }, "");
#else
    GTEST_SKIP() << "now()'s precondition is only enforced via assert() in debug builds.";
#endif
}

TEST(RelativeClockTest, AbsoluteNsAtRelativeZeroMatchesStartTime) {
    relative_clock clock;
    clock.start();
    int64_t abs_ns_at_zero = clock.absolute_ns(time_point{});
    int64_t start_ns       = clock.start_time().time_since_epoch().count();
    ASSERT_EQ(abs_ns_at_zero, start_ns);
}

TEST(DurationToDoubleTest, ConvertsNanosecondsToSeconds) {
    duration d{std::chrono::seconds{2}};
    ASSERT_NEAR(duration_to_double(d), 2.0, 1e-9);
}

TEST(DurationToDoubleTest, ConvertsToMillisecondsWithExplicitUnit) {
    duration d{std::chrono::milliseconds{250}};
    ASSERT_NEAR((duration_to_double<std::milli>(d)), 250.0, 1e-6);
}

TEST(FreqToPeriodTest, ComputesPeriodFromFrequency) {
    duration p = freq_to_period(1000.0); // 1000 Hz -> 1,000,000 ns period.
    ASSERT_EQ(p.count(), 1'000'000);
}

} // namespace

} // namespace ILLIXR
