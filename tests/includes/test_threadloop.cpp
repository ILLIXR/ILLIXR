/**
 * @brief Unit tests for threadloop (include/illixr/threadloop.hpp).
 */

#include "illixr/phonebook.hpp"
#include "illixr/record_logger.hpp"
#include "illixr/stoplight.hpp"
#include "illixr/threadloop.hpp"

#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace ILLIXR {

namespace {

/// plugin's (and hence threadloop's) constructor looks up a record_logger unconditionally; this
/// stands in for one. See test_plugin.cpp for why log() needs `using record_logger::log;`.
class mock_record_logger : public record_logger {
public:
    using record_logger::log;

    void log(const record& r) override {
        r.mark_used();
    }
};

/// Polls pred until it returns true or timeout elapses, to avoid brittle fixed-sleep assertions
/// about what a background thread has or hasn't done yet.
template<typename Predicate>
bool wait_until(Predicate pred, std::chrono::milliseconds timeout) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return pred();
}

class test_threadloop : public threadloop {
public:
    using threadloop::skip_option; // re-export as public so test code can name it

    test_threadloop(const std::string& name, phonebook* pb)
        : threadloop{name, pb} { }

    std::atomic<int>         setup_count{0};
    std::atomic<int>         iteration_count{0};
    std::atomic<int>         skip_check_count{0};
    std::atomic<skip_option> next_skip{skip_option::run};

protected:
    void _p_thread_setup() override {
        setup_count++;
    }

    skip_option _p_should_skip() override {
        skip_check_count++;
        return next_skip.load();
    }

    void _p_one_iteration() override {
        iteration_count++;
    }
};

/// Signals should_stop and calls stop(), satisfying stop()'s precondition. threadloop's
/// destructor asserts the thread has already been joined, so every test that calls start() must
/// go through this before the test function returns.
void shutdown(test_threadloop& tl, stoplight& sl) {
    sl.signal_should_stop();
    tl.stop();
}

class ThreadloopTest : public ::testing::Test {
protected:
    void SetUp() override {
        pb_ = std::make_unique<phonebook>();
        pb_->register_impl<record_logger>(std::make_shared<mock_record_logger>());
        pb_->register_impl<stoplight>(std::make_shared<stoplight>());
        sl_ = pb_->lookup_impl<stoplight>();
    }

    std::unique_ptr<phonebook> pb_;
    std::shared_ptr<stoplight> sl_;
};

// RAII guard that calls shutdown() when it goes out of scope, regardless of how the enclosing
// test function exits. This matters specifically because GTest's ASSERT_* macros return early
// from the current function on failure -- without this, any assertion failure between start()
// and an explicit shutdown() call at the end of a test skips that shutdown entirely, leaving the
// thread running when tl is destructed and aborting the whole test process via threadloop's own
// destructor assert, rather than just failing the one test.
class threadloop_guard {
public:
    threadloop_guard(test_threadloop& tl, stoplight& sl)
        : tl_{tl}
        , sl_{sl} { }

    ~threadloop_guard() {
        shutdown(tl_, sl_);
    }

    threadloop_guard(const threadloop_guard&)            = delete;
    threadloop_guard& operator=(const threadloop_guard&) = delete;

private:
    test_threadloop& tl_;
    stoplight&        sl_;
};

TEST(ThreadloopConstructionTest, ThrowsIfStoplightIsNotRegistered) {
    phonebook pb;
    pb.register_impl<record_logger>(std::make_shared<mock_record_logger>());
    // No stoplight registered.
    ASSERT_THROW(test_threadloop("no_stoplight", &pb), std::exception);
}

TEST_F(ThreadloopTest, RunsSetupThenIterationsAfterReady) {
    test_threadloop  tl{"test_threadloop", pb_.get()};
    threadloop_guard guard{tl, *sl_};
    tl.start();

    ASSERT_FALSE(wait_until([&tl]() { return tl.setup_count.load() > 0; }, std::chrono::milliseconds{20}));

    sl_->signal_ready();

    ASSERT_TRUE(wait_until([&tl]() { return tl.setup_count.load() > 0; }, std::chrono::milliseconds{500}));
    ASSERT_TRUE(wait_until([&tl]() { return tl.iteration_count.load() > 0; }, std::chrono::milliseconds{500}));
}

TEST_F(ThreadloopTest, SkipAndYieldNeverRunsAnIteration) {
    test_threadloop  tl{"test_threadloop", pb_.get()};
    threadloop_guard guard{tl, *sl_};
    tl.next_skip = test_threadloop::skip_option::skip_and_yield;
    tl.start();
    sl_->signal_ready();

    ASSERT_TRUE(wait_until([&tl]() { return tl.skip_check_count.load() > 5; }, std::chrono::milliseconds{500}));
    ASSERT_EQ(tl.iteration_count.load(), 0);
}

TEST_F(ThreadloopTest, InternalStopHaltsTheLoopWithoutSignalingShouldStop) {
    test_threadloop  tl{"test_threadloop", pb_.get()};
    threadloop_guard guard{tl, *sl_};
    tl.start();
    sl_->signal_ready();

    ASSERT_TRUE(wait_until([&tl]() { return tl.iteration_count.load() > 0; }, std::chrono::milliseconds{500}));

    tl.internal_stop();

    // Let whatever iteration was already in flight when internal_stop() was called finish -- the
    // flag is only checked at the top of the loop, not mid-iteration.
    std::this_thread::sleep_for(std::chrono::milliseconds{20});

    // Compare two samples taken after that settle window, rather than an immediate
    // post-internal_stop() snapshot against a later one -- that single-snapshot comparison is
    // exactly the race that caused the flaky failure here.
    int first_sample = tl.iteration_count.load();
    std::this_thread::sleep_for(std::chrono::milliseconds{30});
    int second_sample = tl.iteration_count.load();

    ASSERT_FALSE(sl_->check_should_stop()); // confirms this stopped via internal_stop(), not the stoplight
    ASSERT_EQ(first_sample, second_sample);
}

TEST_F(ThreadloopTest, SkipOptionStopExitsBeforeAnyIteration) {
    test_threadloop  tl{"test_threadloop", pb_.get()};
    threadloop_guard guard{tl, *sl_};
    tl.next_skip = test_threadloop::skip_option::stop;
    tl.start();
    sl_->signal_ready();

    ASSERT_TRUE(wait_until([&tl]() { return tl.setup_count.load() > 0; }, std::chrono::milliseconds{500}));
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
    ASSERT_EQ(tl.iteration_count.load(), 0);
}

TEST_F(ThreadloopTest, StopAssertsIfShouldStopWasNeverSignaled) {
#ifndef NDEBUG
    // Everything, including start(), happens inside the forked child (ASSERT_DEATH's lambda).
    // fork() only clones the calling thread, so if tl's worker thread were spawned in the parent
    // before forking, the child would inherit a std::thread with no real OS thread behind it --
    // joining or destroying that is undefined behavior, not a clean assert.
    //
    // Defined as its own statement rather than inline inside ASSERT_DEATH: the macro's
    // argument-splitting only tracks parenthesis nesting, not braces, so the brace-init list's
    // comma below (tl{"...", &pb}) would otherwise be misread as separating macro arguments.
    auto attempt = []() {
      phonebook pb;
      pb.register_impl<record_logger>(std::make_shared<mock_record_logger>());
      pb.register_impl<stoplight>(std::make_shared<stoplight>());
      auto sl = pb.lookup_impl<stoplight>();

      test_threadloop tl{"test_threadloop", &pb};
      tl.start();
      sl->signal_ready();
      std::this_thread::sleep_for(std::chrono::milliseconds{20});

      tl.stop(); // should_stop was never signaled -- assert() fires here
    };
    ASSERT_DEATH(attempt(), "");
#else
    GTEST_SKIP() << "stop()'s precondition is only enforced via assert() in debug builds.";
#endif
}
} // namespace

} // namespace ILLIXR
