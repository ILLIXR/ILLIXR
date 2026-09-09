#pragma once
// clang-format off
#include "illixr/network/udpsocket.hpp"
// clang-format on

#include "illixr/network/network_backend.hpp"
#include "illixr/network/topic_config.hpp"
#include "illixr/plugin.hpp"
#include "illixr/switchboard.hpp"

#include <chrono>
#include <cstdint>
#include <thread>
#include <unordered_map>
#include <vector>

namespace ILLIXR {

/**
 * Switchboard UDP transport for tracking and small control messages.
 * Each logical topic message uses the original single-datagram format;
 * large payloads such as video frames use the TCP backend.
 */
class MY_EXPORT_API udp_network_backend
    : public plugin
    , public network::udp_backend {
public:
    explicit udp_network_backend(const std::string& name_, phonebook* pb_);
    ~udp_network_backend() override;
#ifdef __ANDROID__
    void start() override;
#else
    void start_client() override;
    void start_server() override;
#endif
    void read_loop(network::UDPSocket* socket);
    void topic_create(std::string topic_name, network::topic_config& config) override;
    bool is_topic_networked(std::string topic_name) override;
    void topic_send(std::string topic_name, std::string&& message) override;
    void topic_receive(const std::string& topic_name, std::vector<char>& message);
    void stop() override;

    network::topic_config::TransportMethod transport_method() const override {
        return network::topic_config::TransportMethod::UDP;
    }

    bool client;

private:
    /** Send one topic packet without application-level fragmentation. */
    void send_packet(std::string&& packet);

    /** Validate and dispatch a received topic packet. */
    void receive_packet(std::string&& packet);

    /** Send a backend-control packet on the illixr_control topic. */
    void send_control(const std::string& message);

    std::shared_ptr<switchboard> switchboard_;
    std::atomic<bool>            running_ = true;
#ifndef __ANDROID__
    std::atomic<bool> ready_ = false;
#endif
    network::UDPSocket* peer_socket_ = nullptr;
    std::thread         io_thread_;

    std::string server_ip_;
    int         server_port_{9003};
    std::string client_ip_;
    int         client_port_{9002};
    int         is_client_{0};

    std::vector<std::string>                               networked_topics_;
    std::unordered_map<std::string, network::topic_config> networked_topics_configs_;

    // To delimit the topic_name and the serialization method when creating a topic
    std::string delimiter_ = "|";
};

} // namespace ILLIXR
