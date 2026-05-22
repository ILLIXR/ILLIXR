#pragma once

#include "illixr/data_format/latency_data.hpp"
#include "illixr/data_format/serialization/latency.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace ILLIXR {

/**
 * @brief Network latency measurement server plugin (RX side).
 *
 * This plugin listens for ping packets from a client, and immediately
 * responds with pong packets containing timing information. This allows
 * the client to measure round-trip network latency.
 *
 * No local switchboard publishing is done here — the server side
 * time conversion data arrives via the combined_pose topic instead.
 */
class MY_EXPORT_API network_latency_rx : public threadloop {
public:
    /**
     * @brief Construct the network latency server plugin.
     * @param name_ Plugin name
     * @param pb_ Phonebook pointer for accessing services
     */
    [[maybe_unused]] network_latency_rx(const std::string& name, phonebook* pb);

    /**
     * @brief Destructor - ensures clean shutdown.
     */
    ~network_latency_rx() override;

protected:
    /**
     * @brief Determines if the iteration should be skipped.
     * @return skip_option indicating whether to run or skip
     */
    skip_option _p_should_skip() override;

    /**
     * @brief Main iteration - checks for pings and sends pong responses.
     */
    void _p_one_iteration() override;

private:
    /**
     * @brief Process a received ping packet and send a pong response.
     * @param ping The received ping packet
     */
    void process_ping(const switchboard::ptr<const data_format::latency_ping>& ping);

    /**
     * @brief Get current timestamp in nanoseconds.
     * @return Current time in nanoseconds since epoch
     */
    static uint64_t get_timestamp_ns();

    // Switchboard
    const std::shared_ptr<switchboard> switchboard_;

    // Buffered reader for receiving pings (tcp_network_backend populates this topic)
    switchboard::buffered_reader<data_format::latency_ping> ping_reader_;

    // Network writer for sending pongs (transmitted over network by tcp_network_backend)
    switchboard::network_writer<data_format::latency_pong> pong_writer_;

    // State
    std::atomic<uint64_t>   pings_received_;
    std::optional<uint64_t> last_received_seq_; ///< Empty until first ping received
};

} // namespace ILLIXR
