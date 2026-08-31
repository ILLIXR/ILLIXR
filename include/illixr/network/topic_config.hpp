#pragma once

#include <chrono>
#include <optional>

namespace ILLIXR::network {

struct topic_config {
    enum priority_type { LOWEST, LOW, MEDIUM, HIGH, HIGHEST };

    enum packetization_type { IMMEDIATE, DEFAULT, SUGGEST_LATENCY };

    priority_type                                         priority           = MEDIUM;
    bool                                                  retransmit         = false;
    bool                                                  allow_out_of_order = false;
    packetization_type                                    packetization      = DEFAULT;
    std::optional<std::chrono::duration<long, std::nano>> latency;

    enum SerializationMethod { BOOST, PROTOBUF } serialization_method;

    enum TransportMethod { TCP, UDP } transport_method;

    topic_config() = default;

    topic_config(SerializationMethod method, TransportMethod transport = TCP)
        : serialization_method{method}
        , transport_method{transport} { }
};

} // namespace ILLIXR::network
