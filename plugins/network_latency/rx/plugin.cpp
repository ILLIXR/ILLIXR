#include "plugin.hpp"

#include "illixr/plugin.hpp"

#include <chrono>
#include <optional>
#include <spdlog/spdlog.h>

using namespace ILLIXR;
using namespace ILLIXR::data_format;

network_latency_rx::network_latency_rx(const std::string& name_, phonebook* pb_)
        : threadloop{name_, pb_}
        , switchboard_{pb_->lookup_impl<switchboard>()}
        , ping_reader_{switchboard_->get_buffered_reader<latency_ping>("latency_ping")}
        , pong_writer_{switchboard_->get_network_writer<latency_pong>("latency_pong")}
        , pings_received_{0}
        , last_received_seq_{std::nullopt} {
    spdlog::get("illixr")->info("[network_latency_rx] Initialized");
}

network_latency_rx::~network_latency_rx() {
    spdlog::get("illixr")->debug("[network_latency_rx] Destructor called, processed {} pings",
                                 pings_received_.load());
}

threadloop::skip_option network_latency_rx::_p_should_skip() {
    // Always run to check for incoming pings with minimal latency
    // The threadloop will yield appropriately
    return skip_option::run;
}

void network_latency_rx::_p_one_iteration() {
    // Process all available pings from the buffered reader
    while (true) {
        auto ping = ping_reader_.dequeue();
        if (!ping) {
            break;
        }

        // Skip if we've already processed this sequence number (duplicate detection)
        if (last_received_seq_.has_value() && ping->sequence_number <= last_received_seq_.value()) {
            spdlog::get("illixr")->trace("[network_latency_rx] Skipping duplicate seq={}", ping->sequence_number);
            continue;
        }

        process_ping(ping);
        last_received_seq_ = ping->sequence_number;
    }
}

void network_latency_rx::process_ping(const switchboard::ptr<const latency_ping>& ping) {
    // Record server timestamp immediately for minimal additional latency
    uint64_t server_timestamp = get_timestamp_ns();

    // Create and send pong response
    pong_writer_.put(
            pong_writer_.allocate<latency_pong>(latency_pong{ping->sequence_number,
                                                             ping->client_timestamp_ns,
                                                             server_timestamp}));
    //spdlog::get("illixr")->debug("[pong] hs: {}   sv: {}", ping->client_timestamp_ns, server_timestamp);
    pings_received_.fetch_add(1);

    //spdlog::get("illixr")->info("[network_latency_rx] Responded to ping seq={}", ping->sequence_number);
}

uint64_t network_latency_rx::get_timestamp_ns() {
    auto now = std::chrono::system_clock::now();
    auto ns  = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch());
    return static_cast<uint64_t>(ns.count());
}

// Plugin entry point
PLUGIN_MAIN(network_latency_rx)
