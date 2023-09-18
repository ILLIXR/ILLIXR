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
private:
    std::atomic<bool>     _m_stop{false};
    std::thread           _m_thread;
    std::function<void()> _m_body;
    std::function<void()> _m_on_start;
    std::function<void()> _m_on_stop;

    void thread_main() {
        assert(_m_body);
        if (_m_on_start) {
            _m_on_start();
        }
        while (!this->_m_stop.load()) {
            _m_body();
        }
        if (_m_on_stop) {
            _m_on_stop();
        }
    }

public:
    /**
     * @brief Constructs a non-startable thread
     */
    managed_thread() noexcept = default;

    /**
     * @brief Constructs a startable thread
     *
     * @p on_stop is called once (if present)
     * @p on_start is called as the thread is joining
     * @p body is called in a tight loop
     */
    explicit managed_thread(std::function<void()> body, std::function<void()> on_start = std::function<void()>{},
                            std::function<void()> on_stop = std::function<void()>{}) noexcept
        : _m_body{std::move(body)}
        , _m_on_start{std::move(on_start)}
        , _m_on_stop{std::move(on_stop)} { }

    /**
     * @brief Stops a thread, if necessary
     */
    ~managed_thread() noexcept {
        if (get_state() == state::running) {
            stop();
        }
        assert(get_state() == state::stopped || get_state() == state::startable || get_state() == state::nonstartable);
        // assert(!_m_thread.joinable());
    }

    /// Possible states for a managed_thread
    enum class state {
        nonstartable,
        startable,
        running,
        stopped,
    };

    /**
     */
    state get_state() {
        bool stopped = _m_stop.load();
        if (!_m_body) {
            return state::nonstartable;
        } else if (!stopped && !_m_thread.joinable()) {
            return state::startable;
        } else if (!stopped && _m_thread.joinable()) {
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
        _m_thread = std::thread{&managed_thread::thread_main, this};
        assert(get_state() == state::running);
    }

    /**
     * @brief Moves a managed_thread from running to stopped
     */
    void stop() {
        assert(get_state() == state::running);
        _m_stop.store(true);
        _m_thread.join();
        assert(get_state() == state::stopped);
    }
};

} // namespace ILLIXR
