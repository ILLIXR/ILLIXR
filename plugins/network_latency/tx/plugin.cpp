#include "plugin.hpp"

#include "illixr/plugin.hpp"

#include <chrono>
#include <optional>
#include <spdlog/spdlog.h>

using namespace ILLIXR;
using namespace ILLIXR::data_format;

// Default configuration values
static constexpr int DEFAULT_PING_INTERVAL_MS = 100;

[[maybe_unused]] network_latency_tx::network_latency_tx(const std::string& name, phonebook* pb)
        : threadloop{name, pb}
        , switchboard_{pb->lookup_impl<switchboard>()}
        , ping_writer_{switchboard_->get_network_writer<latency_ping>("latency_ping")}
        , pong_rx_{std::make_shared<network_latency_pong_rx>(name, pb)}
        , sequence_number_{0}
        , last_ping_time_{std::chrono::steady_clock::now()} {
    // Read ping interval from environment
    int interval_ms = switchboard_->get_env_int("NETWORK_LATENCY_INTERVAL_MS", DEFAULT_PING_INTERVAL_MS);
    ping_interval_  = std::chrono::milliseconds(interval_ms);

    spdlog::get("illixr")->info("[network_latency_tx] Initialized with interval={}ms", interval_ms);
}

void network_latency_tx::start() {
    threadloop::start();
    pong_rx_->start();
}

void network_latency_tx::stop() {
    pong_rx_->stop();
    threadloop::stop();
}

threadloop::skip_option network_latency_tx::_p_should_skip() {
    auto now     = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_ping_time_);

    if (elapsed >= ping_interval_) {
        return skip_option::run;
    }

    return skip_option::skip_and_yield;
}

void network_latency_tx::_p_one_iteration() {
    uint64_t seq       = sequence_number_.fetch_add(1);
    uint64_t timestamp = get_timestamp_ns();

    ping_writer_.put(ping_writer_.allocate<latency_ping>(latency_ping{seq, timestamp}));

    last_ping_time_ = std::chrono::steady_clock::now();

    spdlog::get("illixr")->info("[network_latency_tx] Sent ping seq={}", seq);
}

uint64_t network_latency_tx::get_timestamp_ns() {
    auto now = std::chrono::system_clock::now();
    auto ns  = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch());
    return static_cast<uint64_t>(ns.count());
}

// Plugin entry point
PLUGIN_MAIN(network_latency_tx)