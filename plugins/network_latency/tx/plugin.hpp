#pragma once

#include "illixr/data_format/latency_data.hpp"
#include "illixr/data_format/serialization/latency.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"

#include "pong_rx/plugin.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace ILLIXR {

/**
 * @brief Network latency measurement client plugin (TX side).
 *
 * This plugin sends ping packets to a server and receives pong responses
 * via the pong_rx subplugin. The pong_rx plugin calculates round-trip
 * network latency and clock offset, publishing results to the switchboard
 * for other plugins to consume.
 *
 * The tcp_network_backend plugin handles the actual network transport.
 * This plugin publishes pings to a switchboard topic (which tcp_network_backend
 * transmits over the network) and reads pongs from another topic (which
 * tcp_network_backend populates from network data).
 *
 * Environment Variables:
 *   - NETWORK_LATENCY_INTERVAL_MS: Interval between pings in milliseconds (default: 100)
 */
class network_latency_tx : public threadloop {
public:
    /**
     * @brief Construct the network latency client plugin.
     * @param name_ Plugin name
     * @param pb_ Phonebook pointer for accessing services
     */
    [[maybe_unused]] network_latency_tx(const std::string& name, phonebook* pb);

    void start() override;
    void stop() override;
protected:
    /**
     * @brief Determines if the iteration should be skipped.
     * @return skip_option indicating whether to run or skip
     */
    skip_option _p_should_skip() override;

    /**
     * @brief Main iteration - sends ping and processes any received pongs.
     */
    void _p_one_iteration() override;

private:

    /**
     * @brief Get current timestamp in nanoseconds.
     * @return Current time in nanoseconds since epoch
     */
    static uint64_t get_timestamp_ns();

    // Switchboard
    const std::shared_ptr<switchboard> switchboard_;

    // Network writer for sending pings (transmitted over network by tcp_network_backend)
    switchboard::network_writer<data_format::latency_ping> ping_writer_;

    std::shared_ptr<network_latency_pong_rx> pong_rx_;

    // Configuration
    std::chrono::milliseconds ping_interval_;

    // State
    std::atomic<uint64_t>                 sequence_number_;
    std::chrono::steady_clock::time_point last_ping_time_;
};

} // namespace ILLIXR
