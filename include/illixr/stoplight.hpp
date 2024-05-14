#pragma once

#include "phonebook.hpp"

#include <atomic>
#include <condition_variable>
#include <mutex>

namespace ILLIXR {

/**
 * @brief A boolean condition-variable.
 *
 * Inspired by https://docs.python.org/3/library/threading.html#event-objects
 */
class event {
public:
    /**
     * @brief Sets the condition-variable to new_value.
     *
     * Defaults to true, so that set() sets the bool.
     */
    void set(bool new_value = true) {
        {
            std::lock_guard lock{mutex_};
            value_ = new_value;
        }
        if (new_value) {
            cv_.notify_all();
        }
    }

    /**
     * @brief Clears the condition-variable.
     */
    void clear() {
        set(false);
    }

    /**
     * @brief Test if is set without blocking.
     */
    bool is_set() const {
        return value_;
    }

    /**
     * @brief Wait indefinitely for the event to be set.
     */
    void wait() const {
        std::unique_lock<std::mutex> lock{mutex_};
        // Check if we even need to wait
        if (value_) {
            return;
        }
        cv_.wait(lock, [this] {
            return value_.load();
        });
    }

    /**
     * @brief Wait for the event to be set with a timeout.
     *
     * Returns whether the event was actually set.
     */
    template<class Clock, class Rep, class Period>
    [[maybe_unused]] bool wait_timeout(const std::chrono::duration<Rep, Period>& duration) const {
        auto timeout_time = Clock::now() + duration;
        if (value_) {
            return true;
        }
        std::unique_lock<std::mutex> lock{mutex_};
        while (cv_.wait_until(lock, timeout_time) != std::cv_status::timeout) {
            if (value_) {
                return true;
            }
        }
        return false;
    }

private:
    mutable std::mutex              mutex_;
    mutable std::condition_variable cv_;
    std::atomic<bool>               value_ = false;

};

/**
 * @brief Start/stop synchronization for the whole application.
 *
 * Threads should:
 * 1. Do initialization actions.
 * 2. Wait for ready()
 * 3. Do their main work in a loop until should_stop().
 * 4. Do their shutdown actions.
 *
 * The main thread should:
 * 1. Construct and start all plugins and construct all services.
 * 2. Set ready().
 * 3. Wait for shutdown_complete().
 *
 * The stopping thread should:
 * 1. Someone should set should_stop().
 * 2. stop() and destruct each plugin and destruct each service.
 * 3. Set shutdown_complete().
 */
class stoplight : public phonebook::service {
public:
    void wait_for_ready() const {
        ready_.wait();
    }

    void signal_ready() {
        ready_.set();
    }

    bool check_should_stop() const {
        return should_stop_.is_set();
    }

    void signal_should_stop() {
        should_stop_.set();
    }

    void wait_for_shutdown_complete() const {
        shutdown_complete_.wait();
    }

    bool check_shutdown_complete() const {
        return shutdown_complete_.is_set();
    }

    void signal_shutdown_complete() {
        shutdown_complete_.set();
    }

private:
    event ready_;
    event should_stop_;
    event shutdown_complete_;
};

} // namespace ILLIXR
