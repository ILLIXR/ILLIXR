#include "illixr/data_format.hpp"
#include "illixr/network/net_config.hpp"
#include "illixr/network/network_backend.hpp"
#include "illixr/network/socket.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"
#include "illixr/network/file_descriptor.hpp"

#include <iostream>
#include <map>

using namespace ILLIXR;

struct topic_connection {
    topic_config config;
    TCPSocket*   socket;
};

std::string DELIMITER = "END!END!END!";

class tcp_network_backend
    : public plugin
    , public network_backend {
public:
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

        std::thread(&tcp_network_backend::listen_for_connections, this).detach();
    }

    void topic_create(std::string topic_name, topic_config config) override {
        Address    other_addr = Address(peer_ip, peer_port);
        TCPSocket* socket     = new TCPSocket();
        socket->set_reuseaddr();
        socket->enable_no_delay();

        bool success = false;
        while (!success) {
            try {
                socket->connect(other_addr);
                success = true;
            } catch (unix_error& e) {
            	print_exception(e);
                std::cout << "Connection failed, retrying in 1 second" << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }

        std::string data_to_be_sent = "topic_create:" + topic_name;

        socket->write(data_to_be_sent + DELIMITER);
        topic_map[topic_name] = topic_connection{config, socket};
    }

    bool is_topic_networked(std::string topic_name) override {
        if (topic_map.find(topic_name) == topic_map.end()) {
            return false;
        } else {
            return true;
        }
    }

    void topic_send(std::string topic_name, std::vector<char>& message) override {
        if (is_topic_networked(topic_name) == false) {
            std::cout << "Topic not networked" << std::endl;
            return;
        }

        std::string      data_to_be_sent = std::string(message.begin(), message.end());
        topic_connection topic           = topic_map[topic_name];

        topic.socket->write(data_to_be_sent + DELIMITER);
    }

    // Helper function to queue a received message into the corresponding topic
    void topic_receive(std::string topic_name, std::vector<char>& message) {
        if (!sb->topic_exists(topic_name)) {
            return;
        }

	std::cout << "received topic" << std::endl;
        sb->get_topic(topic_name).deserialize_and_put(message);
    }

    void stop() override {
        running = false;
    }

private:
    std::shared_ptr<switchboard>            sb;
    std::map<std::string, topic_connection> topic_map;
    bool                                    running = true;

    void listen_for_connections() {
        std::cout << "Listening for connections" << std::endl;
        Address   my_addr = Address(self_ip, self_port);
        TCPSocket socket;
        socket.set_reuseaddr();
        socket.bind(my_addr);
        socket.enable_no_delay();
        socket.listen();

        while (running) {
            TCPSocket* read_socket = new TCPSocket(FileDescriptor(system_call(
                "accept",
                ::accept(socket.fd_num(), nullptr, nullptr)))); /* Blocking operation, waiting for client to connect */
            std::cout << "Connection is established with " << read_socket->peer_address().str(":") << std::endl;

            std::thread(&tcp_network_backend::handle_connections, this, read_socket).detach();
        }
    }

    void handle_connections(TCPSocket* read_socket) {
        std::string buffer_str = "";
        std::string topic_name = "";

        while (running) {
            std::string recv_data = read_socket->read(); /* Blocking operation, wait for the data to come */

            buffer_str = buffer_str + recv_data;
            if (recv_data.size() > 0) {
                std::string::size_type end_position = buffer_str.find(DELIMITER);
                while (end_position != std::string::npos) {
                    std::string before = buffer_str.substr(0, end_position);
                    buffer_str         = buffer_str.substr(end_position + DELIMITER.size());

                    // process the data
                    if (before.substr(0, 13) == "topic_create:") {
                        topic_name            = before.substr(13);
                        topic_map[topic_name] = topic_connection{topic_config{}, read_socket};
                    } else {
                    	std::vector<char> message{before.begin(), before.end()};
                        topic_receive(topic_name, message);
                    }
                    end_position = buffer_str.find(DELIMITER);
                }
            }
        }
    }

private:
    std::string self_ip   = "0.0.0.0";
    int         self_port = 22222;
    std::string peer_ip;
    int         peer_port = 22222;
};

extern "C" plugin* this_plugin_factory(phonebook* pb) {
    auto plugin_ptr = std::make_shared<tcp_network_backend>("tcp_network_backend", pb);
    pb->register_impl<network_backend>(plugin_ptr);
    auto* obj = plugin_ptr.get();
    return obj;
}
