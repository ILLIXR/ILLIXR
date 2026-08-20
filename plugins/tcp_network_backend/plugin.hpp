#pragma once
// clang-format off
#include "illixr/network/tcpsocket.hpp"
// clang-format on

#include "illixr/network/network_backend.hpp"
#include "illixr/network/topic_config.hpp"
#include "illixr/plugin.hpp"
#include "illixr/switchboard.hpp"

namespace ILLIXR {

class MY_EXPORT_API tcp_network_backend
    : public plugin
    , public network::tcp_backend {
public:
    explicit tcp_network_backend(const std::string& name_, phonebook* pb_);
#ifdef __ANDROID__
    void start() override;
#else
    void start_client() override;
    void start_server() override;
#endif
    void read_loop(network::TCPSocket* socket);
    void topic_create(std::string topic_name, network::topic_config& config) override;
    bool is_topic_networked(std::string topic_name) override;
    void topic_send(std::string topic_name, std::string&& message) override;
    void topic_receive(const std::string& topic_name, std::vector<char>& message);
    void stop() override;
#ifdef __ANDROID__
    ~tcp_network_backend() override;
#endif
    network::topic_config::TransportMethod transport_method() const override {
        return network::topic_config::TransportMethod::TCP;
    }

    bool client;

private:
    void send_to_peer(const std::string& topic_name, std::string&& message);

    std::shared_ptr<switchboard> switchboard_;
    std::atomic<bool>            running_ = true;
#ifndef __ANDROID__
    std::atomic<bool> ready_ = false;
#endif
    network::TCPSocket* peer_socket_ = nullptr;

    std::string server_ip_;
    int         server_port_;
    std::string client_ip_;
    int         client_port_;
    int         is_client_;

    std::vector<std::string>                               networked_topics_;
    std::unordered_map<std::string, network::topic_config> networked_topics_configs_;
#ifdef __ANDROID__
    network::TCPSocket server_socket_;
#endif
    // To delimit the topic_name and the serialization method when creating a topic
    std::string delimiter_ = "|";
};

} // namespace ILLIXR
