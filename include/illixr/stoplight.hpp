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
class Event {
private:
    mutable std::mutex              _m_mutex;
    mutable std::condition_variable _m_cv;
    std::atomic<bool>               _m_value = false;

public:
    /**
     * @brief Sets the condition-variable to new_value.
     *
     * Defaults to true, so that set() sets the bool.
     */
    void set(bool new_value = true) {
        {
            std::lock_guard lock{_m_mutex};
            _m_value = new_value;
        }
        if (new_value) {
            _m_cv.notify_all();
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
        return _m_value;
    }

    /**
     * @brief Wait indefinitely for the event to be set.
     */
    void wait() const {
        std::unique_lock<std::mutex> lock{_m_mutex};
        // Check if we even need to wait
        if (_m_value) {
            return;
        }
        _m_cv.wait(lock, [this] {
            return _m_value.load();
        });
    }

    /**
     * @brief Wait for the event to be set with a timeout.
     *
     * Returns whether the event was actually set.
     */
    template<class Clock, class Rep, class Period>
    bool wait_timeout(const std::chrono::duration<Rep, Period>& duration) const {
        auto timeout_time = Clock::now() + duration;
        if (_m_value) {
            return true;
        }
        std::unique_lock<std::mutex> lock{_m_mutex};
        while (_m_cv.wait_until(lock, timeout_time) != std::cv_status::timeout) {
            if (_m_value) {
                return true;
            }
        }
        return false;
    }
};

/**
 * @brief Start/stop synchronization for the whole application.
 *
 * Threads should:
 * 1. Do intiailization actions.
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
class Stoplight : public phonebook::service {
public:
    void wait_for_ready() const {
        _m_ready.wait();
    }

    void signal_ready() {
        _m_ready.set();
    }

    bool check_should_stop() const {
        return _m_should_stop.is_set();
    }

    void signal_should_stop() {
        _m_should_stop.set();
    }

    void wait_for_shutdown_complete() const {
        _m_shutdown_complete.wait();
    }

    bool check_shutdown_complete() const {
        return _m_shutdown_complete.is_set();
    }

    void signal_shutdown_complete() {
        _m_shutdown_complete.set();
    }

private:
    Event _m_ready;
    Event _m_should_stop;
    Event _m_shutdown_complete;
};

} // namespace ILLIXR
