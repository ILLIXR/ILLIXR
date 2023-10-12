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
        : network_backend(name_, pb_) { }

    void topic_create(std::string topic_name, topic_config config) override { }

    bool is_topic_networked(std::string topic_name) override {
        return false;
    }

    void topic_send(std::string topic_name, std::vector<char> message) override { }

    std::shared_ptr<std::vector<char>> topic_get(std::string topic_name) override {
        return std::shared_ptr<std::vector<char>>();
    }

    std::shared_ptr<std::vector<char>> topic_dequeue(std::string topic_name) override {
        return std::shared_ptr<std::vector<char>>();
    }
};

PLUGIN_MAIN(tcp_network_backend)