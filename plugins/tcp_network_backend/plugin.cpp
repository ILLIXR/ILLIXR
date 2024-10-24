#include "illixr/data_format.hpp"
#include "illixr/network/net_config.hpp"
#include "illixr/network/network_backend.hpp"
#include "illixr/network/socket.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"
#include "illixr/network/file_descriptor.hpp"

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
            std::thread([this]() { start_client(); }).detach();

            // wait till we are connected
            while (!ready) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }

        if (self_port != 0) {
            client = false;
            std::thread([this]() { start_server(); }).detach();

            // The "ready" will be true anyway once the client is started
            while (!ready) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }

    void start_client() {
        Address    other_addr = Address(peer_ip, peer_port);
        TCPSocket* socket     = new TCPSocket();
        socket->set_reuseaddr();
        socket->enable_no_delay();
        peer_socket = socket;

        bool success = false;
        while (!success) {
            try {
                socket->connect(other_addr);
                success = true;
            } catch (unix_error& e) {
                std::cout << "Connection failed to " + peer_ip + ", " + std::to_string(peer_port) + ", retrying in 1 second" << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }

        std::cout << "Connected to peer: " << peer_ip << ":" << peer_port << std::endl;

        ready = true;
        read_loop(socket);
    }

    void start_server() {
        Address   self_addr = Address(self_ip, self_port);
        TCPSocket server_socket;
        server_socket.set_reuseaddr();
        server_socket.bind(self_addr);
        server_socket.enable_no_delay();
        server_socket.listen();

        TCPSocket client_socket = server_socket.accept();
        std::cout << "Accepted connection from peer: " << client_socket.peer_address().str(":") << std::endl;
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
            std::string packet = socket->read();
            buffer += packet;

            // check if we have a complete packet
            while (buffer.size() >= 8) {
                uint32_t total_length = *reinterpret_cast<uint32_t*>(buffer.data());
                if (buffer.size() >= total_length) {
                    uint32_t topic_name_length = *reinterpret_cast<uint32_t*>(buffer.data() + 4);
                    std::string topic_name(buffer.data() + 8, topic_name_length);
                    std::vector<char> message(buffer.begin() + 8 + topic_name_length, buffer.begin() + total_length);
                    topic_receive(topic_name, message);
                    buffer.erase(buffer.begin(), buffer.begin() + total_length);
                } else {
                    break;
                }
            }
        }
    }

    void topic_create(std::string topic_name, topic_config config) override {
        networked_topics.push_back(topic_name);
        send_to_peer("illixr_control", "create_topic " + topic_name);
    }

    bool is_topic_networked(std::string topic_name) override {
        return std::find(networked_topics.begin(), networked_topics.end(), topic_name) != networked_topics.end();
    }

    void topic_send(std::string topic_name, std::vector<char>& message) override {
        if (is_topic_networked(topic_name) == false) {
            std::cout << "Topic not networked" << std::endl;
            return;
        }

        std::cout << "Sending to peer: " << topic_name << std::endl;
        send_to_peer(topic_name, std::string(message.begin(), message.end()));
    }

    // Helper function to queue a received message into the corresponding topic
    void topic_receive(std::string topic_name, std::vector<char>& message) {
        if (topic_name == "illixr_control") {
            std::string message_str(message.begin(), message.end());
            // check if message starts with "create_topic"
            if (message_str.find("create_topic") == 0) {
                std::string topic_name = message_str.substr(12);
                networked_topics.push_back(topic_name);
                std::cout << "Received create_topic for " << topic_name << std::endl;
            }
            return;
        }

        if (!sb->topic_exists(topic_name)) {
            return;
        }

        sb->get_topic(topic_name).deserialize_and_put(message);
    }

    void stop() override {
        running = false;
    }

private:
    std::shared_ptr<switchboard>            sb;
    std::atomic<bool> running = true;
    std::atomic<bool> ready  = false;
    TCPSocket* peer_socket;

    std::string self_ip   = "0.0.0.0";
    int         self_port = 22222;
    std::string peer_ip;
    int         peer_port = 22222;

    std::vector<std::string> networked_topics;

    void send_to_peer(std::string topic_name, std::string message) {
        // packet are in the format
        // total_length:4bytes|topic_name_length:4bytes|topic_name|message
        uint32_t total_length = 8 + topic_name.size() + message.size();
        std::string packet;
        packet.append(reinterpret_cast<char*>(&total_length), 4);
        uint32_t topic_name_length = topic_name.size();
        packet.append(reinterpret_cast<char*>(&topic_name_length), 4);
        packet.append(topic_name);
        packet.append(message.begin(), message.end());
        peer_socket->write(packet);
    }
};

extern "C" plugin* this_plugin_factory(phonebook* pb) {
    auto plugin_ptr = std::make_shared<tcp_network_backend>("tcp_network_backend", pb);
    pb->register_impl<network_backend>(plugin_ptr);
    auto* obj = plugin_ptr.get();
    return obj;
}
