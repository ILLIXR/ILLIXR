#include "plugin.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

using namespace ILLIXR;

namespace {

// Like ALVR's stream socket, ILLIXR splits each logical message into
// MTU-safe shards. Keeping this prefix small matters for video, where every
// encoded frame consists of many datagrams.
constexpr std::uint16_t kShardMagic      = 0x5258; // "XR" on little-endian hosts
constexpr std::size_t   kShardPrefixSize = 2 * sizeof(std::uint16_t) + 3 * sizeof(std::uint32_t);

template<typename T>
T load_wire_value(const char* source) {
    T value{};
    std::memcpy(&value, source, sizeof(value));
    return value;
}

template<typename T>
void append_wire_value(std::string& destination, T value) {
    destination.append(reinterpret_cast<const char*>(&value), sizeof(value));
}

// Compare 32-bit sequence numbers across wraparound while their distance stays
// below half the number space (the standard serial-number arithmetic rule).
bool wrapping_less(std::uint32_t left, std::uint32_t right) {
    return static_cast<std::int32_t>(left - right) < 0;
}

std::uint16_t stream_id_for_topic(const std::string& topic) {
    // Stable FNV-1a stream identifier. Zero is reserved for control traffic.
    std::uint32_t hash = 2166136261u;
    for (const unsigned char byte : topic) {
        hash ^= byte;
        hash *= 16777619u;
    }
    std::uint16_t stream_id = static_cast<std::uint16_t>((hash >> 16u) ^ hash);
    return stream_id == 0 ? 1 : stream_id;
}

} // namespace

udp_network_backend::udp_network_backend(const std::string& name_, phonebook* pb_)
    : plugin(name_, pb_)
    , switchboard_{pb_->lookup_impl<switchboard>()} {
    // read environment variables
    if (switchboard_->get_env_char("ILLIXR_SERVER_IP")) {
        server_ip_ = switchboard_->get_env_char("ILLIXR_SERVER_IP");
        spdlog::get("illixr")->info("[udp_network_backend] Using server IP {}", server_ip_);
    } else if (switchboard_->get_env_char("ILLIXR_UDP_SERVER_IP")) {
        server_ip_ = switchboard_->get_env_char("ILLIXR_UDP_SERVER_IP");
        spdlog::get("illixr")->info("[udp_network_backend] Using UDP server IP {}", server_ip_);
    } else if (switchboard_->get_env_char("ILLIXR_TCP_SERVER_IP")) {
        server_ip_ = switchboard_->get_env_char("ILLIXR_TCP_SERVER_IP");
        spdlog::get("illixr")->info("[udp_network_backend] Using TCP/UDP server IP {}", server_ip_);
    }

    if (switchboard_->get_env_char("ILLIXR_UDP_SERVER_PORT")) {
        server_port_ = std::stoi(switchboard_->get_env_char("ILLIXR_UDP_SERVER_PORT"));
        spdlog::get("illixr")->info("[udp_network_backend] Using UDP server port {}", server_port_);
    }

    if (switchboard_->get_env_char("ILLIXR_CLIENT_IP")) {
        client_ip_ = switchboard_->get_env_char("ILLIXR_CLIENT_IP");
        spdlog::get("illixr")->info("[udp_network_backend] Using client IP {}", client_ip_);
    } else if (switchboard_->get_env_char("ILLIXR_UDP_CLIENT_IP")) {
        client_ip_ = switchboard_->get_env_char("ILLIXR_UDP_CLIENT_IP");
        spdlog::get("illixr")->info("[udp_network_backend] Using UDP client IP {}", client_ip_);
    } else if (switchboard_->get_env_char("ILLIXR_TCP_CLIENT_IP")) {
        client_ip_ = switchboard_->get_env_char("ILLIXR_TCP_CLIENT_IP");
        spdlog::get("illixr")->info("[udp_network_backend] Using TCP/UDP client IP {}", client_ip_);
    }

    if (switchboard_->get_env_char("ILLIXR_UDP_CLIENT_PORT")) {
        client_port_ = std::stoi(switchboard_->get_env_char("ILLIXR_UDP_CLIENT_PORT"));
        spdlog::get("illixr")->info("[udp_network_backend] Using UDP client port {}", client_port_);
    }

    packet_size_ = static_cast<std::size_t>(std::clamp(switchboard_->get_env_int("ILLIXR_UDP_PACKET_SIZE", 1400), 576, 65507));
    socket_buffer_size_ = static_cast<std::size_t>(
        std::max(switchboard_->get_env_int("ILLIXR_UDP_SOCKET_BUFFER_BYTES", 4 * 1024 * 1024), 64 * 1024));

    if (switchboard_->get_env_char("ILLIXR_IS_CLIENT")) {
        is_client_ = std::stoi(switchboard_->get_env_char("ILLIXR_IS_CLIENT"));
        spdlog::get("illixr")->info("[udp_network_backend] Is client {}", is_client_);
    } else {
        is_client_ = 0;
    }

    if (is_client_) {
        client = true;
        // Android needs to hand the threads differently
#ifdef __ANDROID__
        auto* socket = new network::UDPSocket();
        socket->socket_set_reuseaddr();
        socket->socket_set_buffer_sizes(static_cast<int>(socket_buffer_size_));
        socket->socket_set_receive_timeout(100);
        if (switchboard_->get_env_bool("ILLIXR_UDP_DSCP", "true"))
            socket->socket_set_dscp_expedited_forwarding();
        // Always bind so the OS assigns a local port, making the client reachable for
        // server -> client datagrams (e.g., future round-trip topics).  If
        // ILLIXR_UDP_CLIENT_PORT is set, bind to that specific port; otherwise bind to
        // port 0 and let the OS assign an ephemeral port.
        if (!client_ip_.empty())
            socket->socket_bind(client_ip_, client_port_);
        else if (client_port_ != 0)
            socket->socket_bind(client_port_);
        else
            socket->socket_bind(0);

        socket->set_peer(server_ip_, server_port_);
        peer_socket_ = socket;

        spdlog::get("illixr")->info("[udp_network_backend] Connecting to {}:{}", server_ip_, server_port_);
        // UDP is connectionless � set_peer() is sufficient; no connect() needed
        spdlog::get("illixr")->info("[udp_network_backend] Client ready");
#else
        io_thread_ = std::thread([this]() {
            start_client();
        });

        // wait till we are connected
        while (!ready_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

#endif
    } else {
        client = false;
        // Android needs to handl;e the threads differently
#ifdef __ANDROID__
        auto* socket = new network::UDPSocket();
        socket->socket_set_reuseaddr();
        socket->socket_set_buffer_sizes(static_cast<int>(socket_buffer_size_));
        socket->socket_set_receive_timeout(100);
        if (switchboard_->get_env_bool("ILLIXR_UDP_DSCP", "true"))
            socket->socket_set_dscp_expedited_forwarding();
        socket->socket_bind(server_ip_, server_port_);

        spdlog::get("illixr")->info("[udp_network_backend] Listening on UDP port {}", server_port_);

        peer_socket_ = socket;
#else
        io_thread_ = std::thread([this]() {
            start_server();
        });

        while (!ready_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

#endif
    }
}

#ifdef __ANDROID__
void udp_network_backend::start() {
    plugin::start();
    io_thread_ = std::thread([this]() {
        read_loop(peer_socket_);
    });
}
#else

void udp_network_backend::start_client() {
    auto* socket = new network::UDPSocket();
    socket->socket_set_reuseaddr();
    socket->socket_set_buffer_sizes(static_cast<int>(socket_buffer_size_));
    socket->socket_set_receive_timeout(100);
    if (switchboard_->get_env_bool("ILLIXR_UDP_DSCP", "true"))
        socket->socket_set_dscp_expedited_forwarding();
    // Always bind so the OS assigns a local port, making the client reachable for
    // server -> client datagrams (e.g., future round-trip topics).  If
    // ILLIXR_UDP_CLIENT_PORT is set, bind to that specific port; otherwise bind to
    // port 0 and let the OS assign an ephemeral port.
    if (!client_ip_.empty())
        socket->socket_bind(client_ip_, client_port_);
    else if (client_port_ != 0)
        socket->socket_bind(client_port_);
    else
        socket->socket_bind(0);
    socket->set_peer(server_ip_, server_port_);
    peer_socket_ = socket;

    spdlog::get("illixr")->info("[udp_network_backend] Connecting to {}:{}", server_ip_, server_port_);
    // UDP is connectionless - set_peer() is sufficient; no connect() needed
    spdlog::get("illixr")->info("[udp_network_backend] Client ready");

    ready_ = true;
    read_loop(socket);
}

void udp_network_backend::start_server() {
    auto* socket = new network::UDPSocket();
    socket->socket_set_reuseaddr();
    socket->socket_set_buffer_sizes(static_cast<int>(socket_buffer_size_));
    socket->socket_set_receive_timeout(100);
    if (switchboard_->get_env_bool("ILLIXR_UDP_DSCP", "true"))
        socket->socket_set_dscp_expedited_forwarding();
    socket->socket_bind(server_ip_, server_port_);

    // If the client's address is already known from the environment (rather than
    // needing to be learned dynamically), set it now so we can send to the client
    // even if it never sends us anything first. Otherwise, read_loop() falls back
    // to learning the peer from the first datagram it receives.
    if (!client_ip_.empty() && client_port_ != 0) {
        socket->set_peer(client_ip_, client_port_);
        spdlog::get("illixr")->info("[udp_network_backend] Pre-configured peer {}:{}", client_ip_, client_port_);
    }

    spdlog::get("illixr")->info("[udp_network_backend] Listening on UDP port {}", server_port_);

    peer_socket_ = socket;
    ready_       = true;
    read_loop(socket);
}
#endif

void udp_network_backend::read_loop(network::UDPSocket* socket) {
    std::string buffer;
    while (running_) {
        // Each recvfrom() returns exactly one datagram - no partial-read reassembly needed
        sockaddr_in src_addr{};
        std::string packet = socket->read_data(&src_addr);
        if (packet.empty())
            continue;

        // The client already has its peer set from construction, so has_peer() is
        // true and this is a no-op there. The server, however, does not know the
        // client's address until it receives a datagram from it, since UDP is
        // connectionless; learn the peer here so topic_send/send_control have
        // somewhere to reply to.
        if (!socket->has_peer())
            socket->set_peer(src_addr);

        receive_packet(std::move(packet));
    }
}

void udp_network_backend::topic_create(std::string topic_name, network::topic_config& config) {
    networked_topics_.push_back(topic_name);
    networked_topics_configs_[topic_name] = config;
    spdlog::get("illixr")->info("[udp_network_backend] topic_create: {}", topic_name);
    // Notify the peer of the new topic and its serialization method, mirroring
    // the TCP backend's illixr_control handshake.  Since UDP is unreliable we
    // send it a few times to reduce the chance of loss before data arrives.
    if (peer_socket_ != nullptr && peer_socket_->has_peer()) {
        std::string serialization =
            (config.serialization_method == network::topic_config::SerializationMethod::BOOST) ? "BOOST" : "PROTOBUF";
        std::string ctrl_message = "create_topic" + topic_name + delimiter_ + serialization;

        for (int i = 0; i < 3; ++i)
            send_control(ctrl_message);
    } else {
        spdlog::get("illixr")->error("[udp_network_backend]: ERROR socket: {}  has_peer: {}",
                                     (peer_socket_ == nullptr) ? "null" : "valid", peer_socket_->has_peer());
    }
}

bool udp_network_backend::is_topic_networked(std::string topic_name) {
    return std::find(networked_topics_.begin(), networked_topics_.end(), topic_name) != networked_topics_.end();
}

void udp_network_backend::topic_send(std::string topic_name, std::string&& message) {
    if (!is_topic_networked(topic_name)) {
        spdlog::get("illixr")->warn("[udp_network_backend] topic_send: {} not networked", topic_name);
        return;
    }
    // Packet format: total_length(4) | topic_name_length(4) | topic_name | message
    auto     topic_name_length = static_cast<uint32_t>(topic_name.size());
    uint32_t total_length      = 8u + topic_name_length + static_cast<uint32_t>(message.size());

    std::string packet;
    packet.reserve(total_length);
    packet.append(reinterpret_cast<const char*>(&total_length), 4);
    packet.append(reinterpret_cast<const char*>(&topic_name_length), 4);
    packet.append(topic_name);
    packet.append(message);

    send_packet(stream_id_for_topic(topic_name), std::move(packet));
}

void udp_network_backend::send_packet(std::uint16_t stream_id, std::string&& packet) {
    if (peer_socket_ == nullptr || !peer_socket_->has_peer() || packet_size_ <= kShardPrefixSize) {
        spdlog::get("illixr")->warn("[udp_network_backend] Cannot send UDP packet: peer unavailable or packet size invalid");
        return;
    }

    const std::size_t shard_payload_size = packet_size_ - kShardPrefixSize;
    const std::size_t shard_count_size   = (packet.size() + shard_payload_size - 1) / shard_payload_size;
    if (shard_count_size == 0 || shard_count_size > std::numeric_limits<std::uint32_t>::max()) {
        spdlog::get("illixr")->warn("[udp_network_backend] Message is too large to shard ({} bytes)", packet.size());
        return;
    }

    const std::uint32_t message_id  = next_message_id_.fetch_add(1, std::memory_order_relaxed);
    const std::uint32_t shard_count = static_cast<std::uint32_t>(shard_count_size);

    // Serialize one datagram at a time. A per-shard lock allows small tracking
    // messages from other writers to interleave with a large video frame.
    for (std::uint32_t shard_index = 0; shard_index < shard_count; ++shard_index) {
        const std::size_t offset = static_cast<std::size_t>(shard_index) * shard_payload_size;
        const std::size_t size   = std::min(shard_payload_size, packet.size() - offset);

        std::string shard;
        shard.reserve(kShardPrefixSize + size);
        append_wire_value(shard, kShardMagic);
        append_wire_value(shard, stream_id);
        append_wire_value(shard, message_id);
        append_wire_value(shard, shard_count);
        append_wire_value(shard, shard_index);
        shard.append(packet.data() + offset, size);

        std::lock_guard<std::mutex> lock(send_mutex_);
        if (!peer_socket_->write_data(shard)) {
            spdlog::get("illixr")->warn("[udp_network_backend] Failed to send message {} shard {}/{}", message_id,
                                        shard_index + 1, shard_count);
            return;
        }
    }
}

// Reassembly is deliberately single-threaded in read_loop(). Only socket sends
// require a mutex because multiple switchboard network writers may publish.
void udp_network_backend::receive_packet(std::string&& packet) {
    // Accept legacy single-datagram packets so this backend remains compatible
    // with peers built before MTU-safe packetization was added.
    if (packet.size() < kShardPrefixSize || load_wire_value<std::uint16_t>(packet.data()) != kShardMagic) {
        receive_complete_packet(std::move(packet));
        return;
    }

    const std::uint16_t stream_id  = load_wire_value<std::uint16_t>(packet.data() + sizeof(std::uint16_t));
    const std::uint32_t message_id = load_wire_value<std::uint32_t>(packet.data() + 2 * sizeof(std::uint16_t));
    const std::uint32_t shard_count =
        load_wire_value<std::uint32_t>(packet.data() + 2 * sizeof(std::uint16_t) + sizeof(std::uint32_t));
    const std::uint32_t shard_index =
        load_wire_value<std::uint32_t>(packet.data() + 2 * sizeof(std::uint16_t) + 2 * sizeof(std::uint32_t));
    const std::size_t payload_size = packet.size() - kShardPrefixSize;
    const std::size_t max_payload  = packet_size_ - kShardPrefixSize;

    if (shard_count == 0 || shard_index >= shard_count || payload_size > max_payload ||
        static_cast<std::uint64_t>(shard_count) * max_payload > MAX_MESSAGE_BYTES) {
        spdlog::get("illixr")->warn("[udp_network_backend] Invalid shard metadata for message {}", message_id);
        return;
    }

    const std::uint64_t message_key = (static_cast<std::uint64_t>(stream_id) << 32u) | message_id;
    auto                found       = in_progress_messages_.find(message_key);
    if (found == in_progress_messages_.end()) {
        if (in_progress_messages_.size() >= MAX_CONCURRENT_MESSAGES) {
            auto oldest =
                std::min_element(in_progress_messages_.begin(), in_progress_messages_.end(), [](const auto& a, const auto& b) {
                    return a.second.first_received < b.second.first_received;
                });
            if (oldest != in_progress_messages_.end()) {
                in_progress_messages_.erase(oldest);
            }
        }
        in_progress_message state;
        state.buffer.resize(static_cast<std::size_t>(shard_count) * max_payload);
        state.received_shards.resize(shard_count, false);
        state.shard_count    = shard_count;
        state.first_received = std::chrono::steady_clock::now();
        found                = in_progress_messages_.emplace(message_key, std::move(state)).first;
    }

    in_progress_message& state = found->second;
    if (state.shard_count != shard_count || state.received_shards[shard_index]) {
        return;
    }
    const std::size_t destination_offset = static_cast<std::size_t>(shard_index) * max_payload;
    std::memcpy(state.buffer.data() + destination_offset, packet.data() + kShardPrefixSize, payload_size);
    state.received_shards[shard_index] = true;
    state.received_count++;
    if (shard_index + 1 == shard_count) {
        state.last_shard_size = payload_size;
    }

    if (state.received_count == state.shard_count && state.last_shard_size > 0) {
        const std::size_t complete_size =
            (static_cast<std::size_t>(state.shard_count) - 1) * max_payload + state.last_shard_size;
        state.buffer.resize(complete_size);
        std::string complete(state.buffer.begin(), state.buffer.end());
        in_progress_messages_.erase(found);
        prune_reassembly(stream_id, message_id);
        receive_complete_packet(std::move(complete));
    }
}

void udp_network_backend::prune_reassembly(std::uint16_t stream_id, std::uint32_t completed_message_id) {
    // Once a newer message completes, older partial messages cannot provide
    // useful low-latency data. This mirrors ALVR's latest-complete-packet rule.
    for (auto it = in_progress_messages_.begin(); it != in_progress_messages_.end();) {
        const std::uint16_t candidate_stream  = static_cast<std::uint16_t>(it->first >> 32u);
        const std::uint32_t candidate_message = static_cast<std::uint32_t>(it->first);
        if (candidate_stream == stream_id && wrapping_less(candidate_message, completed_message_id)) {
            it = in_progress_messages_.erase(it);
        } else {
            ++it;
        }
    }
}

void udp_network_backend::receive_complete_packet(std::string&& packet) {
    // Application packet: total_length(4) | topic_name_length(4) | topic_name | message
    if (packet.size() < 8) {
        spdlog::get("illixr")->warn("[udp_network_backend] Undersized message ({} bytes), dropping", packet.size());
        return;
    }

    const std::uint32_t total_length      = load_wire_value<std::uint32_t>(packet.data());
    const std::uint32_t topic_name_length = load_wire_value<std::uint32_t>(packet.data() + 4);
    if (total_length > packet.size() || total_length < 8 + topic_name_length) {
        spdlog::get("illixr")->warn("[udp_network_backend] Truncated message (got={} expected={}), dropping", packet.size(),
                                    total_length);
        return;
    }

    std::string       topic_name(packet.data() + 8, topic_name_length);
    std::vector<char> message(packet.begin() + 8 + topic_name_length, packet.begin() + total_length);
    topic_receive(topic_name, message);
}

// Helper function to queue a received message into the corresponding topic
void udp_network_backend::topic_receive(const std::string& topic_name, std::vector<char>& message) {
    if (topic_name == "illixr_control") {
        std::string message_str(message.begin(), message.end());
        if (message_str.find("create_topic") == 0) {
            size_t d_pos = message_str.find(delimiter_);
            assert(d_pos != std::string::npos);
            std::string l_topic_name  = message_str.substr(12, d_pos - 12);
            std::string serialization = message_str.substr(d_pos + 1);
            networked_topics_.push_back(l_topic_name);
            network::topic_config cfg;
            cfg.serialization_method = (serialization == "BOOST") ? network::topic_config::SerializationMethod::BOOST
                                                                  : network::topic_config::SerializationMethod::PROTOBUF;
            cfg.transport_method     = network::topic_config::TransportMethod::UDP;
            networked_topics_configs_[l_topic_name] = cfg;
            spdlog::get("illixr")->info("[udp_network_backend] Received create_topic for {}", l_topic_name);
        }
        return;
    }
    if (!switchboard_->topic_exists(topic_name)) {
        return;
    }
    auto config = networked_topics_configs_.find(topic_name);
    if (config == networked_topics_configs_.end()) {
        // A producer may start before the UDP peer is known, so its unreliable
        // create_topic announcement can be missed. Data topics used by this
        // backend default to Boost serialization; accepting that default keeps
        // startup order from preventing the first video stream from arriving.
        network::topic_config fallback;
        fallback.serialization_method = network::topic_config::SerializationMethod::BOOST;
        fallback.transport_method     = network::topic_config::TransportMethod::UDP;
        config                        = networked_topics_configs_.emplace(topic_name, fallback).first;
        if (std::find(networked_topics_.begin(), networked_topics_.end(), topic_name) == networked_topics_.end()) {
            networked_topics_.push_back(topic_name);
        }
        spdlog::get("illixr")->info("[udp_network_backend] Inferred Boost/UDP configuration for {}", topic_name);
    }
    switchboard_->get_topic(topic_name).deserialize_and_put(message, config->second);
}

void udp_network_backend::stop() {
    // Wake the timeout-bounded receive loop, then keep the socket alive until
    // its only reader has returned.
    if (!running_.exchange(false)) {
        return;
    }
    if (peer_socket_ != nullptr) {
        peer_socket_->socket_shutdown();
    }
    if (io_thread_.joinable() && io_thread_.get_id() != std::this_thread::get_id()) {
        io_thread_.join();
    }
    delete peer_socket_;
    peer_socket_ = nullptr;
    plugin::stop();
}

udp_network_backend::~udp_network_backend() {
    stop();
}

void udp_network_backend::send_control(const std::string& message) {
    // Send a control datagram directly, bypassing the is_topic_networked guard
    // since illixr_control is not registered as a networked topic.
    auto     topic_name_length = static_cast<uint32_t>(std::strlen("illixr_control"));
    uint32_t total_length      = 8u + topic_name_length + static_cast<uint32_t>(message.size());

    std::string packet;
    packet.reserve(total_length);
    packet.append(reinterpret_cast<const char*>(&total_length), 4);
    packet.append(reinterpret_cast<const char*>(&topic_name_length), 4);
    packet.append("illixr_control");
    packet.append(message);

    send_packet(0, std::move(packet));
}

extern "C" MY_EXPORT_API plugin* this_plugin_factory(phonebook* pb) {
    auto* obj = new udp_network_backend("udp_network_backend", pb);
    // The runtime owns the plugin returned by this factory. Register a non-owning
    // service alias so the phonebook does not try to delete the same object again.
    pb->register_impl<network::udp_backend>(
        std::shared_ptr<network::udp_backend>(static_cast<network::udp_backend*>(obj), [](network::udp_backend*) { }));
    return obj;
}
