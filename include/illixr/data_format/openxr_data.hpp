#pragma once

#include "illixr/switchboard.hpp"

/*
* Wraps a plain C payload so it can travel through switchboard. ILLIXR plugins that produce
* or consume these topics use ixr_event<Payload> as the topic's event type.
 */
template<typename Payload>
struct ixr_event : public ILLIXR::switchboard::event {
    static_assert(std::is_trivially_copyable_v<Payload>, "payloads must be plain C structs");

    Payload data;
    int64_t timestamp_ns; // Monado monotonic clock

    ixr_event() = default;

    ixr_event(const Payload& payload, int64_t timestamp)
        : data{payload}
        , timestamp_ns{timestamp} { }
};

// Must be the same clock domain as Monado's os_monotonic_get_ns.
inline int64_t now_monotonic_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
