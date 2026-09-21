#pragma once

#include <cstdint>
#include <cstring>
#include <string_view>

namespace ILLIXR::network {

inline constexpr std::size_t max_udp_payload_bytes = 65507;

struct udp_packet_view {
    std::string_view topic;
    std::string_view payload;
};

// Decode the original single-datagram envelope without unaligned integer loads.
// Views borrow the packet storage and are valid only while that storage lives.
inline bool decode_udp_packet(std::string_view packet, udp_packet_view& result) noexcept {
    result = {};
    if (packet.size() < 8 || packet.size() > max_udp_payload_bytes) {
        return false;
    }
    std::uint32_t total_length{};
    std::uint32_t topic_length{};
    std::memcpy(&total_length, packet.data(), sizeof(total_length));
    std::memcpy(&topic_length, packet.data() + sizeof(total_length), sizeof(topic_length));
    // Subtract after checking the header size; adding an untrusted topic length
    // to the header size could wrap and allow an out-of-bounds read.
    if (total_length != packet.size() || topic_length > total_length - 8) {
        return false;
    }
    result.topic   = packet.substr(8, topic_length);
    result.payload = packet.substr(8 + topic_length);
    return true;
}

} // namespace ILLIXR::network
