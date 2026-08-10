#pragma once

#include <atomic>
#include <cassert>
#include <functional>
#include <stdexcept>
#include <thread>
#include <utility>

namespace ILLIXR {

/**
 * @brief An object that manages a std::thread; it joins and exits when the object gets destructed.
 */
class managed_thread {
public:
    /// Possible states for a managed_thread
    enum class state {
        nonstartable,
        startable,
        running,
        stopped,
    };

    /**
     * @brief Constructs a non-startable thread
     */
    managed_thread() noexcept = default;

    managed_thread& operator=(const managed_thread& other) = delete;

    managed_thread(const managed_thread& other) = delete;

    /**
     * @brief Constructs a startable thread
     *
     * @p on_stop is called once (if present)
     * @p on_start is called as the thread is joining
     * @p body is called in a tight loop
     */
    explicit managed_thread(std::function<void()> body, std::function<void()> on_start = std::function<void()>{},
                            std::function<void()> on_stop = std::function<void()>{}) noexcept
        : body_{std::move(body)}
        , on_start_{std::move(on_start)}
        , on_stop_{std::move(on_stop)} { }

    /**
     * @brief Stops a thread, if necessary
     */
    ~managed_thread() noexcept {
        if (get_state() == state::running) {
            stop();
        }
        assert(get_state() == state::stopped || get_state() == state::startable || get_state() == state::nonstartable);
        // assert(!thread_.joinable());
    }

    /**
     */
    state get_state() {
        bool stopped = stop_.load();
        if (!body_) {
            return state::nonstartable;
        } else if (!stopped && !thread_.joinable()) {
            return state::startable;
        } else if (!stopped && thread_.joinable()) {
            return state::running;
        } else if (stopped) {
            return state::stopped;
        } else {
            throw std::logic_error{"Unknown state"};
        }
    }

    /**
     * @brief Moves a managed_thread from startable to running
     */
    void start() {
        assert(get_state() == state::startable);
        thread_ = std::thread{&managed_thread::thread_main, this};
        assert(get_state() == state::running);
    }

    /**
     * @brief Moves a managed_thread from running to stopped
     */
    void stop() {
        assert(get_state() == state::running);
        stop_.store(true);
        thread_.join();
        assert(get_state() == state::stopped);
    }

private:
    std::atomic<bool>     stop_{false};
    std::thread           thread_;
    std::function<void()> body_;
    std::function<void()> on_start_;
    std::function<void()> on_stop_;

    void thread_main() {
        assert(body_);
        if (on_start_) {
            on_start_();
        }
        while (!this->stop_.load()) {
            body_();
        }
        if (on_stop_) {
            on_stop_();
        }
    }
};

} // namespace ILLIXR
