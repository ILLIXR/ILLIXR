#pragma once

#include "cpu_timer.hpp"
#include "error_util.hpp"
#include "phonebook.hpp"
#include "plugin.hpp"
#include "record_logger.hpp"
#include "stoplight.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <thread>

namespace ILLIXR {

/**
 * @brief A reusable threadloop for plugins.
 *
 * The thread continuously runs `_p_one_iteration()` and is stoppable by `stop()`.
 *
 * This factors out the common code I noticed in many different plugins.
 */
class threadloop : public plugin {
public:
    threadloop(const std::string& name, phonebook* pb)
        : plugin{name, pb}
        , stoplight_{pb->lookup_impl<stoplight>()} { }

    /**
     * @brief Starts the thread.
     *
     * This cannot go into the constructor because it starts a thread which calls
     * `_p_one_iteration()` which is virtual in the child class.
     *
     * Calling a virtual child method from the parent constructor will not work as expected
     * [1]. Instead, the ISO CPP FAQ recommends calling a `start()` method immediately after
     * construction [2].
     *
     * [1]: https://stackoverflow.com/questions/962132/calling-virtual-functions-inside-constructors
     * [2]: https://isocpp.org/wiki/faq/strange-inheritance#calling-virtuals-from-ctor-idiom
     */
    void start() override {
        plugin::start();
        thread_ = std::thread([this] {
            thread_main();
        });
        assert(!stoplight_->check_should_stop());
        assert(thread_.joinable());
    }

    /**
     * @brief Joins the thread.
     *
     * Must have already stopped the stoplight.
     */
    void stop() override {
        assert(stoplight_->check_should_stop());
        // only join if it has been started
        if (thread_.joinable())
            thread_.join();
        plugin::stop();
    }

    /**
     * @brief Stops the thread.
     *
     * A thread should call this if it wants to stop itself (due to out of data for example).
     */
    virtual void internal_stop() {
        internal_stop_.store(true);
    }

    ~threadloop() override {
        assert(stoplight_->check_should_stop());
        assert(!thread_.joinable());
    }

protected:
    enum class skip_option {
        /// Run iteration NOW. Only then does CPU timer begin counting.
        run,

        /// AKA "busy wait". Skip but try again very quickly.
        skip_and_spin,

        /// Yielding gives up a scheduling quantum, which is determined by the OS, but usually on
        /// the order of 1-10ms. This is nicer to the other threads in the system.
        skip_and_yield,

        /// Calls stop.
        stop,
    };

    /**
     * @brief Gets called in a tight loop, to gate the invocation of `_p_one_iteration()`
     */
    virtual skip_option _p_should_skip() {
        return skip_option::run;
    }

    /**
     * @brief Gets called at setup time, from the new thread.
     */
    virtual void _p_thread_setup() { }

    /**
     * @brief Override with the computation the thread does every loop.
     *
     * This gets called in rapid succession.
     */
    virtual void _p_one_iteration() = 0;

    /**
     * @brief Whether the thread has been asked to terminate.
     *
     * Check this before doing long-running computation; it makes termination more responsive.
     */
    bool should_terminate() {
        return internal_stop_.load();
    }

    std::size_t iteration_no = 0;
    std::size_t skip_no      = 0;

private:
    void thread_main() {

        // TODO: In the future, synchronize the main loop instead of the setup.
        // This is currently not possible because relative_clock is required in
        // some setup functions, and relative_clock is only guaranteed to be
        // available once `wait_for_ready()` unblocks.
        stoplight_->wait_for_ready();
        _p_thread_setup();

        while (!stoplight_->check_should_stop() && !should_terminate()) {
            skip_option s = _p_should_skip();

            switch (s) {
            case skip_option::skip_and_yield:
                std::this_thread::yield();
                ++skip_no;
                break;
            case skip_option::skip_and_spin:
                ++skip_no;
                break;
            case skip_option::run: {
                auto iteration_start_cpu_time  = thread_cpu_time();
                auto iteration_start_wall_time = std::chrono::high_resolution_clock::now();

                RAC_ERRNO();
                _p_one_iteration();
                RAC_ERRNO();

                ++iteration_no;
                skip_no = 0;
                break;
            }
            case skip_option::stop:
                // Break out of the switch AND the loop
                // See https://stackoverflow.com/questions/27788326/breaking-out-of-nested-loop-c
                goto break_loop;
            }
        }
    break_loop:
        [[maybe_unused]] int cpp_requires_a_statement_after_a_label_plz_optimize_me_away;
    }

    std::atomic<bool>                internal_stop_{false};
    std::thread                      thread_;
    std::shared_ptr<const stoplight> stoplight_;
};

} // namespace ILLIXR
