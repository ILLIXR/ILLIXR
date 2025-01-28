#include "illixr/data_format.hpp"
#include "illixr/network/net_config.hpp"
#include "illixr/network/network_backend.hpp"
#include "illixr/network/tcpsocket.hpp"
#include "illixr/network/topic_config.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"

#include <map>

using namespace ILLIXR;

class tcp_network_backend
    : public plugin
    , public network_backend {
public:
    bool client;

    explicit tcp_network_backend(std::string name_, phonebook* pb_)
        : plugin(name_, pb_)
        , sb{pb->lookup_impl<switchboard>()} {
        // read environment variables
        if (std::getenv("ILLIXR_TCP_HOST_IP")) {
            self_ip = std::getenv("ILLIXR_TCP_HOST_IP");
        }

        if (std::getenv("ILLIXR_TCP_HOST_PORT")) {
            self_port = std::stoi(std::getenv("ILLIXR_TCP_HOST_PORT"));
        }

        if (std::getenv("ILLIXR_TCP_PEER_IP")) {
            peer_ip = std::getenv("ILLIXR_TCP_PEER_IP");
        }

        if (std::getenv("ILLIXR_TCP_PEER_PORT")) {
            peer_port = std::stoi(std::getenv("ILLIXR_TCP_PEER_PORT"));
        }

        if (peer_port != 0) {
            client = true;
            std::thread([this]() {
                start_client();
            }).detach();

            // wait till we are connected
            while (!ready) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }

        if (self_port != 0) {
            client = false;
            std::thread([this]() {
                start_server();
            }).detach();

            while (!ready) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }

    void start_client() {
        TCPSocket* socket     = new TCPSocket();
        socket->socket_set_reuseaddr();
        socket->enable_no_delay();
        peer_socket = socket;

        std::cout << "Connecting to " + peer_ip + " at port " + std::to_string(peer_port) << std::endl;
        socket->socket_connect(peer_ip, peer_port);
        std::cout << "Connected to server" << std::endl;

        ready = true;
        read_loop(socket);
    }

    void start_server() {
        TCPSocket server_socket;
        server_socket.socket_set_reuseaddr();
        server_socket.socket_bind(self_ip, self_port);
        server_socket.enable_no_delay();
        server_socket.socket_listen();

        TCPSocket client_socket = server_socket.socket_accept();
        std::cout << "Accepted connection from peer: " << client_socket.peer_address() << std::endl;
        peer_socket = &client_socket;
        ready       = true;
        read_loop(&client_socket);
    }

    void read_loop(TCPSocket* socket) {
        std::string buffer;
        while (running) {
            // read from socket
            // packet are in the format
            // total_length:4bytes|topic_name_length:4bytes|topic_name|message
            std::string packet = socket->read_data();
            buffer += packet;

            // check if we have a complete packet
            while (buffer.size() >= 8) {
                uint32_t total_length = *reinterpret_cast<uint32_t*>(buffer.data());
                if (buffer.size() >= total_length) {
                    uint32_t          topic_name_length = *reinterpret_cast<uint32_t*>(buffer.data() + 4);
                    std::string       topic_name(buffer.data() + 8, topic_name_length);
                    std::vector<char> message(buffer.begin() + 8 + topic_name_length, buffer.begin() + total_length);
                    topic_receive(topic_name, message);
                    buffer.erase(buffer.begin(), buffer.begin() + total_length);
                } else {
                    break;
                }
            }
        }
    }

    void topic_create(std::string topic_name, topic_config& config) override {
        networked_topics.push_back(topic_name);
        networked_topics_configs[topic_name] = config;
        std::string serializaiton;
        if (config.serialization_method == topic_config::SerializationMethod::BOOST) {
            serializaiton = "BOOST";
        } else {
            serializaiton = "PROTOBUF";
        }
        std::string message = "create_topic" + topic_name + delimiter + serializaiton;
        send_to_peer("illixr_control", std::move(message));
    }

    bool is_topic_networked(std::string topic_name) override {
        return std::find(networked_topics.begin(), networked_topics.end(), topic_name) != networked_topics.end();
    }

    void topic_send(std::string topic_name, std::string&& message) override {
        if (is_topic_networked(topic_name) == false) {
            std::cout << "Topic not networked" << std::endl;
            return;
        }

        std::cout << "Sending to peer: " << topic_name << std::endl;
        send_to_peer(topic_name, std::move(message));
    }

    // Helper function to queue a received message into the corresponding topic
    void topic_receive(std::string topic_name, std::vector<char>& message) {
        if (topic_name == "illixr_control") {
            std::string message_str(message.begin(), message.end());
            // check if message starts with "create_topic"
            if (message_str.find("create_topic") == 0) {
                size_t d_pos = message_str.find(delimiter);
                assert(d_pos != std::string::npos);
                std::string topic_name    = message_str.substr(12, d_pos - 12);
                std::string serialization = message_str.substr(d_pos + 1);
                networked_topics.push_back(topic_name);
                topic_config config;
                if (serialization == "BOOST") {
                    config.serialization_method = topic_config::SerializationMethod::BOOST;
                } else {
                    config.serialization_method = topic_config::SerializationMethod::PROTOBUF;
                }
                networked_topics_configs[topic_name] = config;
                std::cout << "Received create_topic for " << topic_name << std::endl;
            }
            return;
        }

        if (!sb->topic_exists(topic_name)) {
            return;
        }

        sb->get_topic(topic_name).deserialize_and_put(message, networked_topics_configs[topic_name]);
    }

    void stop() override {
        running = false;
    }

private:
    std::shared_ptr<switchboard> sb;
    std::atomic<bool>            running = true;
    std::atomic<bool>            ready   = false;
    TCPSocket*                   peer_socket;

    std::string self_ip   = "0.0.0.0";
    int         self_port = 22222;
    std::string peer_ip;
    int         peer_port = 22222;

    std::vector<std::string>                      networked_topics;
    std::unordered_map<std::string, topic_config> networked_topics_configs;

    // To delimit the topic_name and the serialization method when creating a topic
    std::string delimiter = ":";

    void send_to_peer(std::string topic_name, std::string&& message) {
        // packet are in the format
        // total_length:4bytes|topic_name_length:4bytes|topic_name|message
        uint32_t    total_length = 8 + topic_name.size() + message.size();
        std::string packet;
        packet.append(reinterpret_cast<char*>(&total_length), 4);
        uint32_t topic_name_length = topic_name.size();
        packet.append(reinterpret_cast<char*>(&topic_name_length), 4);
        packet.append(topic_name);
        packet.append(message.begin(), message.end());
        peer_socket->write_data(packet);
    }
};

extern "C" plugin* this_plugin_factory(phonebook* pb) {
    auto plugin_ptr = std::make_shared<tcp_network_backend>("tcp_network_backend", pb);
    pb->register_impl<network_backend>(plugin_ptr);
    auto* obj = plugin_ptr.get();
    return obj;
}
