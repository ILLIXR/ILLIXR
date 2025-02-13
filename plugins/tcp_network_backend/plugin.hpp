#pragma once

#include "illixr/network/network_backend.hpp"
#include "illixr/plugin.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/network/tcpsocket.hpp"
#include "illixr/network/topic_config.hpp"

namespace ILLIXR {

class tcp_network_backend
    : public plugin
    , public network::network_backend {
public:
    explicit tcp_network_backend(const std::string& name_, phonebook* pb_);
    void start_client();
    void start_server();
    void read_loop(network::TCPSocket* socket);
    void topic_create(std::string topic_name, network::topic_config& config) override;
    bool is_topic_networked(std::string topic_name) override;
    void topic_send(std::string topic_name, std::string&& message) override;
    void topic_receive(const std::string& topic_name, std::vector<char>& message);
    void stop() override;
    bool client;

private:
    void send_to_peer(const std::string& topic_name, std::string&& message);

    std::shared_ptr<switchboard> switchboard_;
    std::atomic<bool>            running_ = true;
    std::atomic<bool>            ready_   = false;
    network::TCPSocket*          peer_socket_ = nullptr;

    std::string self_ip_   = "0.0.0.0";
    int         self_port_ = 22222;
    std::string peer_ip_;
    int         peer_port_ = 22222;

    std::vector<std::string>                               networked_topics_;
    std::unordered_map<std::string, network::topic_config> networked_topics_configs_;

    // To delimit the topic_name and the serialization method when creating a topic
    std::string delimiter_ = ":";
};

}
