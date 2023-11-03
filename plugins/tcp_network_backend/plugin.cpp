#define VMA_IMPLEMENTATION
#include "illixr/data_format.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"
#include "illixr/network/network_backend.hpp"

using namespace ILLIXR;

class tcp_network_backend : public network_backend {
public:
    explicit tcp_network_backend(std::string name_, phonebook* pb_)
        : network_backend(name_, pb_), sb{pb->lookup_impl<switchboard>()} { }

    void topic_create(std::string topic_name, topic_config config) override { }

    bool is_topic_networked(std::string topic_name) override {
        return false;
    }

    void topic_send(std::string topic_name, std::vector<char> message) override { }

    // Helper function to queue a received message into the corresponding topic
    void topic_receive(std::string topic_name, std::vector<char> message) {
        if (!sb->topic_exists(topic_name)) {
            return;
        }

        sb->get_topic(topic_name).deserialize_and_put(message);
    }

private:
    std::shared_ptr<switchboard> sb;
};

PLUGIN_MAIN(tcp_network_backend)