#include "plugin.hpp"

using namespace ILLIXR;

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
    }

    if (switchboard_->get_env_char("ILLIXR_UDP_CLIENT_PORT")) {
        client_port_ = std::stoi(switchboard_->get_env_char("ILLIXR_UDP_CLIENT_PORT"));
        spdlog::get("illixr")->info("[udp_network_backend] Using UDP client port {}", client_port_);
    }

    if (switchboard_->get_env_char("ILLIXR_IS_CLIENT")) {
        is_client_ = std::stoi(switchboard_->get_env_char("ILLIXR_IS_CLIENT"));
        spdlog::get("illixr")->info("[udp_network_backend] Is client {}", is_client_);
    } else {
        is_client_ = 0;
    }

    if (is_client_) {
        client = true;
        std::thread([this]() {
            start_client();
        }).detach();

        // wait till we are connected
        while (!ready_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    } else {
        client = false;
        std::thread([this]() {
            start_server();
        }).detach();

        while (!ready_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void udp_network_backend::start_client() {
    auto* socket = new network::UDPSocket();
    socket->socket_set_reuseaddr();
    // Always bind so the OS assigns a local port, making the client reachable for
    // server -> client datagrams (e.g., future round-trip topics).  If
    // ILLIXR_UDP_CLIENT_PORT is set, bind to that specific port; otherwise bind to
    // port 0 and let the OS assign an ephemeral port.
    if (!client_ip_.empty())
        socket->socket_bind(client_ip_, client_port_);
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

        // Packet format: total_length(4) | topic_name_length(4) | topic_name | message
        if (packet.size() < 8) {
            spdlog::get("illixr")->warn("[udp_network_backend] Undersized datagram ({} bytes), dropping", packet.size());
            continue;
        }

        uint32_t total_length      = *reinterpret_cast<const uint32_t*>(packet.data());
        uint32_t topic_name_length = *reinterpret_cast<const uint32_t*>(packet.data() + 4);

        if (packet.size() < total_length || total_length < 8 + topic_name_length) {
            spdlog::get("illixr")->warn("[udp_network_backend] Truncated datagram (got={} expected={}), dropping",
                                        packet.size(), total_length);
            continue;
        }

        std::string       topic_name(packet.data() + 8, topic_name_length);
        std::vector<char> message(packet.begin() + 8 + topic_name_length, packet.begin() + total_length);

        topic_receive(topic_name, message);
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

    if (!peer_socket_->write_data(packet))
        spdlog::get("illixr")->warn("[udp_network_backend] write_data failed for topic={}", topic_name);
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
    switchboard_->get_topic(topic_name).deserialize_and_put(message, networked_topics_configs_[topic_name]);
}

void udp_network_backend::stop() {
    running_ = false;
    delete peer_socket_;
    peer_socket_ = nullptr;
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

    if (!peer_socket_->write_data(packet))
        spdlog::get("illixr")->warn("[udp_network_backend] send_control failed");
}

extern "C" MY_EXPORT_API plugin* this_plugin_factory(phonebook* pb) {
    auto plugin_ptr = std::make_shared<udp_network_backend>("udp_network_backend", pb);
    pb->register_impl<network::udp_backend>(std::static_pointer_cast<network::udp_backend>(plugin_ptr));
    auto* obj = plugin_ptr.get();
    return obj;
}
