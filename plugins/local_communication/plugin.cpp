#include "plugin.hpp"

using namespace ILLIXR;

local_network_backend::local_network_backend(const std::string& name_, phonebook* pb_)
    : plugin(name_, pb_)
    , switchboard_{pb_->lookup_impl<switchboard>()} {
    if (switchboard_->get_env_char("ILLIXR_IS_LOCAL_CLIENT")) {
        is_client_ = std::stoi(switchboard_->get_env_char("ILLIXR_IS_LOCAL_CLIENT"));
        spdlog::get("illixr")->info("[local_network_backend] Is client {}", is_client_);
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

void local_network_backend::start_client() {
    auto* socket = new network::TCPSocket();
    socket->socket_bind(client_ip_, client_port_);
    socket->socket_set_reuseaddr();
    socket->enable_no_delay();
    peer_socket_ = socket;

    std::cout << "Connecting to " + server_ip_ + " at port " + std::to_string(server_port_) << std::endl;
    socket->socket_connect(server_ip_, server_port_);
    std::cout << "Connected to server" << std::endl;

    ready_ = true;
    read_loop(socket);
}

void local_network_backend::start_server() {
    network::TCPSocket server_socket;
    server_socket.socket_set_reuseaddr();
    server_socket.socket_bind(server_ip_, server_port_);
    server_socket.enable_no_delay();
    server_socket.socket_listen();

    auto* client_socket = new network::TCPSocket(server_socket.socket_accept());
    std::cout << "Accepted connection from client: " << client_socket->peer_address() << std::endl;
    peer_socket_ = client_socket;
    ready_       = true;
    read_loop(client_socket);
}

void local_network_backend::read_loop(network::TCPSocket* socket) {
    std::string buffer;
    while (running_) {
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

void local_network_backend::topic_create(std::string topic_name, network::topic_config& config) {
    networked_topics_.push_back(topic_name);
    networked_topics_configs_[topic_name] = config;
    std::string serializaiton;
    if (config.serialization_method == network::topic_config::SerializationMethod::BOOST) {
        serializaiton = "BOOST";
    } else {
        serializaiton = "PROTOBUF";
    }
    std::string message = "create_topic" + topic_name + delimiter_ + serializaiton;
    send_to_peer("illixr_control", std::move(message));
}

bool local_network_backend::is_topic_networked(std::string topic_name) {
    return std::find(networked_topics_.begin(), networked_topics_.end(), topic_name) != networked_topics_.end();
}

void local_network_backend::topic_send(std::string topic_name, std::string&& message) {
    if (is_topic_networked(topic_name) == false) {
        std::cout << "Topic not networked" << std::endl;
        return;
    }

    send_to_peer(topic_name, std::move(message));
}

// Helper function to queue a received message into the corresponding topic
void local_network_backend::topic_receive(const std::string& topic_name, std::vector<char>& message) {
    if (topic_name == "illixr_control") {
        std::string message_str(message.begin(), message.end());
        spdlog::get("illixr")->debug("Received {}", message_str);
        // check if the message starts with "create_topic"
        if (message_str.find("create_topic") == 0) {
            size_t d_pos = message_str.find(delimiter_);
            assert(d_pos != std::string::npos);
            std::string l_topic_name  = message_str.substr(12, d_pos - 12);
            std::string serialization = message_str.substr(d_pos + 1);
            networked_topics_.push_back(l_topic_name);
            network::topic_config config;
            if (serialization == "BOOST") {
                config.serialization_method = network::topic_config::SerializationMethod::BOOST;
            } else {
                config.serialization_method = network::topic_config::SerializationMethod::PROTOBUF;
            }
            networked_topics_configs_[l_topic_name] = config;
            spdlog::get("illixr")->debug("Received create_topic for {}", l_topic_name);
        }
        return;
    }

    if (!switchboard_->topic_exists(topic_name)) {
        return;
    }

    switchboard_->get_topic(topic_name).deserialize_and_put(message, networked_topics_configs_[topic_name]);
}

void local_network_backend::stop() {
    running_ = false;
    delete peer_socket_;
}

void local_network_backend::send_to_peer(const std::string& topic_name, std::string&& message) {
    // packets are in the format
    // total_length:4bytes|topic_name_length:4bytes|topic_name|message
    uint32_t    total_length = 8 + topic_name.size() + message.size();
    std::string packet;
    packet.append(reinterpret_cast<char*>(&total_length), 4);
    uint32_t topic_name_length = topic_name.size();
    packet.append(reinterpret_cast<char*>(&topic_name_length), 4);
    packet.append(topic_name);
    packet.append(message.begin(), message.end());
    peer_socket_->write_data(packet);
}

extern "C" MY_EXPORT_API plugin* this_plugin_factory(phonebook* pb) {
    auto plugin_ptr = std::make_shared<local_network_backend>("local_network_backend", pb);
    pb->register_impl<network::local_network_backend>(plugin_ptr);
    auto* obj = plugin_ptr.get();
    return obj;
}
