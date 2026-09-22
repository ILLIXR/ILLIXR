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

    enum SerializationMethod { BOOST, PROTOBUF } serialization_method;

    enum TransportMethod { TCP, UDP } transport_method;

    float min_latency_ms = 0.f;
    float max_latency_ms = 0.f;

    topic_config()
        : serialization_method{BOOST}
        , transport_method{TCP} { }

    explicit topic_config(SerializationMethod method, TransportMethod transport = TCP,
                          float min_latency = 0.f, float max_latency = 0.f)
        : serialization_method{method}
        , transport_method{transport}
        , min_latency_ms{min_latency}
        , max_latency_ms{max_latency} { }
};

} // namespace ILLIXR::network
