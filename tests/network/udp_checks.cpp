#include "illixr/network/udp_packet.hpp"
#include "illixr/network/udpsocket.hpp"

#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace ILLIXR::network;
using namespace std::chrono_literals;

static void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

static std::string packet(std::string_view topic, std::string_view payload) {
    const auto  total      = static_cast<std::uint32_t>(8 + topic.size() + payload.size());
    const auto  topic_size = static_cast<std::uint32_t>(topic.size());
    std::string bytes(reinterpret_cast<const char*>(&total), sizeof(total));
    bytes.append(reinterpret_cast<const char*>(&topic_size), sizeof(topic_size));
    bytes.append(topic);
    bytes.append(payload);
    return bytes;
}

static void check_packet_validation() {
    const std::string payload("pose\0data", 9);
    const std::string valid = packet("quest_controller", payload);
    udp_packet_view   decoded;
    require(decode_udp_packet(valid, decoded), "valid legacy packet rejected");
    require(decoded.topic == "quest_controller" && decoded.payload == payload, "packet data changed");

    // A Windows or ARM build must not rely on the input's integer alignment.
    const std::string unaligned = "x" + valid;
    require(decode_udp_packet(std::string_view(unaligned).substr(1), decoded), "unaligned packet rejected");
    for (std::size_t length = 0; length < valid.size(); ++length) {
        require(!decode_udp_packet(std::string_view(valid).substr(0, length), decoded), "truncated packet accepted");
    }
    require(!decode_udp_packet(valid + "trailing", decoded), "mismatched envelope length accepted");

    std::string         corrupt = valid;
    const std::uint32_t huge    = (std::numeric_limits<std::uint32_t>::max)();
    std::memcpy(corrupt.data() + 4, &huge, sizeof(huge));
    require(!decode_udp_packet(corrupt, decoded), "overflowing topic length accepted");
    require(decoded.topic.empty() && decoded.payload.empty(), "failed decode left stale views");

    const std::string maximum = packet("t", std::string(max_udp_payload_bytes - 9, 'x'));
    require(decode_udp_packet(maximum, decoded), "maximum IPv4 UDP envelope rejected");
    require(!decode_udp_packet(packet("t", std::string(max_udp_payload_bytes - 8, 'x')), decoded),
            "oversized UDP envelope accepted");
}

static void check_sockets() {
    // Exercise concurrent first-time Winsock initialization on Windows.
    std::vector<std::future<void>> startup;
    for (int i = 0; i < 8; ++i) {
        startup.push_back(std::async(std::launch::async, [] {
            UDPSocket socket;
            socket.socket_bind("127.0.0.1", 0);
        }));
    }
    for (auto& result : startup) {
        result.get();
    }

    UDPSocket server;
    server.socket_set_reuseaddr();
    server.socket_bind("127.0.0.1", 0);
    server.socket_set_receive_timeout(100);
    const std::string address = server.local_address();
    const int         port    = std::stoi(address.substr(address.rfind(':') + 1));
    require(port > 0, "ephemeral server port unavailable");

    UDPSocket client;
    client.socket_bind("127.0.0.1", 0);
    client.socket_set_receive_timeout(100);
    require(!client.write_data("no peer"), "send without a peer succeeded");
    client.set_peer("127.0.0.1", port);
    auto peer_updates = std::async(std::launch::async, [&] {
        for (int i = 0; i < 1000; ++i) {
            client.set_peer("127.0.0.1", port);
            require(client.has_peer(), "peer lost during concurrent access");
        }
    });
    for (int i = 0; i < 40; ++i) {
        const std::string data = packet("tracking", std::to_string(i));
        require(client.write_data(data), "tracking send failed");
        sockaddr_in source{};
        require(server.read_data(&source) == data, "tracking datagram changed");
        server.set_peer(source);
        require(server.write_data("ack"), "reply send failed");
        require(client.read_data() == "ack", "reply receive failed");
    }
    peer_updates.get();

    const auto start = std::chrono::steady_clock::now();
    require(server.read_data().empty(), "idle receive returned data");
    const auto elapsed = std::chrono::steady_clock::now() - start;
    require(elapsed >= 20ms && elapsed < 3s, "receive timeout not respected");

    std::atomic<bool>  running{true};
    std::promise<void> started;
    auto               reader = std::async(std::launch::async, [&] {
        started.set_value();
        while (running.load()) {
            (void) server.read_data();
        }
    });
    started.get_future().wait();
    running.store(false);
    server.socket_shutdown();
    require(reader.wait_for(2s) == std::future_status::ready, "receive worker did not stop");
    reader.get();
}

int main() {
    try {
        check_packet_validation();
        check_sockets();
        std::cout << "UDP envelope, loopback, timeout, peer concurrency, and shutdown checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
