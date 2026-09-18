/**
 * @brief Unit tests for managed_thread (include/illixr/managed_thread.hpp).
 */

#include "illixr/managed_thread.hpp"

#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

namespace ILLIXR {

namespace {

TEST(ManagedThreadTest, DefaultConstructedIsNonstartable) {
    managed_thread mt;
    ASSERT_EQ(mt.get_state(), managed_thread::state::nonstartable);
}

TEST(ManagedThreadTest, ConstructedWithBodyIsStartable) {
    managed_thread mt{[]() {
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }};
    ASSERT_EQ(mt.get_state(), managed_thread::state::startable);
}

TEST(ManagedThreadTest, StartMovesToRunningAndBodyRunsRepeatedly) {
    std::atomic<int> counter{0};
    managed_thread   mt{[&counter]() {
      ++counter;
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }};

    mt.start();
    ASSERT_EQ(mt.get_state(), managed_thread::state::running);

    std::this_thread::sleep_for(std::chrono::milliseconds{20});
    mt.stop();

    ASSERT_EQ(mt.get_state(), managed_thread::state::stopped);
    ASSERT_GT(counter.load(), 0);
}

TEST(ManagedThreadTest, OnStartAndOnStopAreCalledOnce) {
    std::atomic<int> start_calls{0};
    std::atomic<int> stop_calls{0};
    std::atomic<int> body_calls{0};

    managed_thread mt{
        [&body_calls]() {
          ++body_calls;
          std::this_thread::sleep_for(std::chrono::milliseconds{1});
        },
        [&start_calls]() {
          ++start_calls;
        },
        [&stop_calls]() {
          ++stop_calls;
        }};

    mt.start();
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
    mt.stop();

    ASSERT_EQ(start_calls.load(), 1);
    ASSERT_EQ(stop_calls.load(), 1);
    ASSERT_GT(body_calls.load(), 0);
}

TEST(ManagedThreadTest, DestructorStopsARunningThread) {
    std::atomic<bool> stopped_cleanly{false};
    {
        managed_thread mt{
            []() {
              std::this_thread::sleep_for(std::chrono::milliseconds{1});
            },
            std::function<void()>{},
            [&stopped_cleanly]() {
              stopped_cleanly = true;
            }};
        mt.start();
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
        // mt goes out of scope here while still running; its destructor must stop it cleanly.
    }
    ASSERT_TRUE(stopped_cleanly.load());
}

} // namespace

} // namespace ILLIXR
