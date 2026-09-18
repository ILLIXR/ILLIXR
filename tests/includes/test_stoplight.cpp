/**
 * @brief Unit tests for stoplight (include/illixr/stoplight.hpp).
 */

#include "illixr/stoplight.hpp"

#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

namespace ILLIXR {

namespace {

TEST(EventTest, DefaultIsNotSet) {
    event e;
    ASSERT_FALSE(e.is_set());
}

TEST(EventTest, SetMakesIsSetTrue) {
    event e;
    e.set();
    ASSERT_TRUE(e.is_set());
}

TEST(EventTest, ClearMakesIsSetFalse) {
    event e;
    e.set();
    e.clear();
    ASSERT_FALSE(e.is_set());
}

TEST(EventTest, SetFalseIsEquivalentToClear) {
    event e;
    e.set();
    e.set(false);
    ASSERT_FALSE(e.is_set());
}

TEST(EventTest, WaitReturnsImmediatelyIfAlreadySet) {
    event e;
    e.set();
    e.wait(); // should not block; a timeout here would hang the whole test suite
    SUCCEED();
}

TEST(EventTest, WaitBlocksUntilSet) {
    event             e;
    std::atomic<bool> waited_returned{false};

    std::thread waiter([&e, &waited_returned]() {
      e.wait();
      waited_returned = true;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds{20});
    ASSERT_FALSE(waited_returned.load()); // still blocked

    e.set();
    waiter.join();
    ASSERT_TRUE(waited_returned.load());
}

TEST(EventTest, WaitTimeoutReturnsTrueWhenSetInTime) {
    event       e;
    std::thread setter([&e]() {
      std::this_thread::sleep_for(std::chrono::milliseconds{10});
      e.set();
    });
    bool result = e.wait_timeout<std::chrono::steady_clock>(std::chrono::milliseconds{500});
    setter.join();
    ASSERT_TRUE(result);
}

TEST(EventTest, WaitTimeoutReturnsFalseWhenNeverSet) {
    event e;
    bool  result = e.wait_timeout<std::chrono::steady_clock>(std::chrono::milliseconds{20});
    ASSERT_FALSE(result);
}

TEST(StoplightTest, ReadySignalWorks) {
    stoplight s;
    s.signal_ready();
    s.wait_for_ready(); // should not block
    SUCCEED();
}

TEST(StoplightTest, ShouldStopSignalWorks) {
    stoplight s;
    ASSERT_FALSE(s.check_should_stop());
    s.signal_should_stop();
    ASSERT_TRUE(s.check_should_stop());
    s.wait_for_should_stop(); // should not block
}

TEST(StoplightTest, ShutdownCompleteSignalWorks) {
    stoplight s;
    ASSERT_FALSE(s.check_shutdown_complete());
    s.signal_shutdown_complete();
    ASSERT_TRUE(s.check_shutdown_complete());
    s.wait_for_shutdown_complete();
}

TEST(StoplightTest, SignalsAreIndependent) {
    stoplight s;
    s.signal_ready();
    ASSERT_FALSE(s.check_should_stop());
    ASSERT_FALSE(s.check_shutdown_complete());
}

} // namespace

} // namespace ILLIXR
