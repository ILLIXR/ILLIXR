#pragma once

#include "illixr/network/network_backend.hpp"
#include "illixr/network/tcpsocket.hpp"
#include "illixr/network/topic_config.hpp"
#include "illixr/plugin.hpp"
#include "illixr/switchboard.hpp"

namespace ILLIXR {

class local_network_backend
    : public plugin
        , public network::local_network_backend {
public:
    explicit local_network_backend(const std::string& name_, phonebook* pb_);
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
    std::atomic<bool>            running_     = true;
    std::atomic<bool>            ready_       = false;
    network::TCPSocket*          peer_socket_ = nullptr;

    const std::string server_ip_ = "127.0.0.1";
    const int         server_port_ = 5590;
    const std::string client_ip_ = "127.0.0.1";
    const int         client_port_ = 5591;
    int               is_client_ = 0;

    std::vector<std::string>                               networked_topics_;
    std::unordered_map<std::string, network::topic_config> networked_topics_configs_;

    // To delimit the topic_name and the serialization method when creating a topic
    std::string delimiter_ = ":";
};

} // namespace ILLIXR
