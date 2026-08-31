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
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace ILLIXR {

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
    struct in_progress_message {
        std::vector<char>                         buffer;
        std::vector<bool>                         received_shards;
        std::uint32_t                             shard_count{0};
        std::uint32_t                             received_count{0};
        std::size_t                               last_shard_size{0};
        std::chrono::steady_clock::time_point     first_received{};
    };

    void send_packet(std::uint16_t stream_id, std::string&& packet);
    void receive_packet(std::string&& packet);
    void receive_complete_packet(std::string&& packet);
    void prune_reassembly(std::uint16_t stream_id, std::uint32_t completed_message_id);
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

    std::size_t                 packet_size_{1400};
    std::size_t                 socket_buffer_size_{4 * 1024 * 1024};
    std::atomic<std::uint32_t>  next_message_id_{0};
    std::mutex                  send_mutex_;
    std::unordered_map<std::uint64_t, in_progress_message> in_progress_messages_;
    static constexpr std::size_t MAX_CONCURRENT_MESSAGES = 10;
    static constexpr std::size_t MAX_MESSAGE_BYTES       = 64 * 1024 * 1024;

    std::vector<std::string>                               networked_topics_;
    std::unordered_map<std::string, network::topic_config> networked_topics_configs_;

    // To delimit the topic_name and the serialization method when creating a topic
    std::string delimiter_ = "|";
};

} // namespace ILLIXR
