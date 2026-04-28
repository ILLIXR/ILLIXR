#pragma once

#include "illixr/switchboard.hpp"
#include <cstdint>


namespace ILLIXR::data_format {

/**
 * @brief Ping packet structure sent over the network for latency measurement.
 *
 * This is a lightweight packet containing only the essential data needed
 * to measure round-trip time between client and server.
 */
struct latency_ping : public switchboard::event {
    uint64_t sequence_number;
    uint64_t client_timestamp_ns; ///< Headset system_clock (Unix epoch ns)

    latency_ping()
        : sequence_number{0}
        , client_timestamp_ns{0} { }

    latency_ping(uint64_t seq, uint64_t ts)
        : sequence_number{seq}
        , client_timestamp_ns{ts} { }
};

/**
 * @brief Pong packet structure sent back from server to client.
 *
 * Contains the original ping data plus server processing timestamp.
 */
struct latency_pong : public switchboard::event {
    uint64_t sequence_number;      ///< Original sequence number from ping
    uint64_t client_timestamp_ns;  ///< Original client timestamp from ping
    uint64_t server_timestamp_ns;  ///< Server timestamp when pong was generated

    latency_pong()
            : sequence_number{0}
            , client_timestamp_ns{0}
            , server_timestamp_ns{0} { }

    latency_pong(uint64_t seq, uint64_t client_ts, uint64_t server_ts)
            : sequence_number{seq}
            , client_timestamp_ns{client_ts}
            , server_timestamp_ns{server_ts} { }
};

struct network_latency_result : public switchboard::event {
    uint64_t sequence_number;
    double   round_trip_time_ms;
    double   estimated_one_way_latency_ms;
    double   client_to_server_ms;
    double   server_to_client_ms;
    double   estimated_clock_offset_ms;
    double   smoothed_clock_offset_ms; ///< EMA smoothed (server - client system_clock)
    double   smoothed_rtt_ms;          ///< EMA smoothed
    uint64_t measurement_timestamp_ns;

    network_latency_result()
        : sequence_number{0}
        , round_trip_time_ms{0.0}
        , estimated_one_way_latency_ms{0.0}
        , client_to_server_ms{0.0}
        , server_to_client_ms{0.0}
        , estimated_clock_offset_ms{0.0}
        , smoothed_clock_offset_ms{0.0}
        , smoothed_rtt_ms{0.0}
        , measurement_timestamp_ns{0} { }

    network_latency_result(uint64_t seq, double rtt_ms, double c2s_ms, double s2c_ms, double clock_offset_ms,
                           double smoothed_offset, double smoothed_rtt, uint64_t ts)
        : sequence_number{seq}
        , round_trip_time_ms{rtt_ms}
        , estimated_one_way_latency_ms{rtt_ms / 2.0}
        , client_to_server_ms{c2s_ms}
        , server_to_client_ms{s2c_ms}
        , estimated_clock_offset_ms{clock_offset_ms}
        , smoothed_clock_offset_ms{smoothed_offset}
        , smoothed_rtt_ms{smoothed_rtt}
        , measurement_timestamp_ns{ts} { }
};
} // namespace ILLIXR::data_format
