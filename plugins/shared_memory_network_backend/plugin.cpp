#include "illixr/data_format.hpp"
#include "illixr/network/network_backend.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"

#include <boost/functional/hash.hpp>
#include <boost/interprocess/ipc/message_queue.hpp>
#include <boost/serialization/binary_object.hpp>
#include <functional>

using namespace ILLIXR;
using namespace boost::interprocess;

#define MAX_MESSAGE_SIZE  (1024 * 1024 * 100) // 100 MB
#define MAX_MESSAGE_COUNT 10

struct message_t {
    std::string       topic_name;
    std::vector<char> message;
    int checksum;
    std::chrono::high_resolution_clock::time_point timestamp;

    template<class Archive>
    void serialize(Archive& ar, const unsigned int version) {
        ar & topic_name;
        ar & message;
        ar & checksum;
        ar & boost::serialization::make_binary_object(&timestamp, sizeof(timestamp));
    }
};

int calculate_checksum(const std::vector<char>& message) {
    // use boost::hash to calculate checksum
    std::size_t seed = 0;
    boost::hash_combine(seed, message);
    return (int) seed;
}

BOOST_CLASS_EXPORT(message_t)

void remove_message_queue() {
    message_queue::remove("illixr_tx");
    message_queue::remove("illixr_rx");
}

class shared_memory_network_backend
    : public threadloop
    , public network_backend {
public:
    explicit shared_memory_network_backend(std::string name_, phonebook* pb_)
        : threadloop(name_, pb_)
        , sb{pb->lookup_impl<switchboard>()}
        , log{spdlogger(nullptr)} {
        std::atexit(remove_message_queue);

        auto mode = std::getenv("ILLIXR_MMAP_BACKEND_MODE");
        if (mode == nullptr) {
            throw std::runtime_error("ILLIXR_MMAP_BACKEND_MODE must be set");
        }

        if (std::string(mode) == "server") {
            server = true;
        } else if (std::string(mode) == "client") {
            server = false;
        } else {
            throw std::runtime_error("ILLIXR_MMAP_BACKEND_MODE must be either 'server' or 'client'");
        }

        // log->info("Shared memory network backend mode: {}", mode);

        if (server) {
            message_queue::remove("illixr_tx");
            message_queue::remove("illixr_rx");

            mq_tx = std::make_shared<message_queue>(open_or_create, "illixr_tx", MAX_MESSAGE_COUNT, MAX_MESSAGE_SIZE);
            mq_rx = std::make_shared<message_queue>(open_or_create, "illixr_rx", MAX_MESSAGE_COUNT, MAX_MESSAGE_SIZE);
        } else {
            // sleep for 1 second to wait for the server to start
            std::this_thread::sleep_for(std::chrono::seconds(1));
            mq_tx = std::make_shared<message_queue>(open_only, "illixr_rx");
            mq_rx = std::make_shared<message_queue>(open_only, "illixr_tx");
        }
    }

    void topic_create(std::string topic_name, topic_config config) override {
        networked_topics[topic_name] = config.priority;
    }

    bool is_topic_networked(std::string topic_name) override {
        return networked_topics.find(topic_name) != networked_topics.end();
    }

    void topic_send(std::string topic_name, std::vector<char> message) override {
        // log->debug("Sending message on topic {}", topic_name);

        if (!is_topic_networked(topic_name)) {
            throw std::runtime_error("Topic " + topic_name + " is not networked");
        }
        message_t msg{topic_name, message, calculate_checksum(message), std::chrono::high_resolution_clock::now()};

        std::vector<char> full_message;
        // serialize using Boost
        boost::iostreams::back_insert_device<std::vector<char>> inserter{full_message};
        boost::iostreams::stream_buffer<boost::iostreams::back_insert_device<std::vector<char>>> stream{
            inserter};
        boost::archive::binary_oarchive oa{stream};
        oa << msg;
        stream.pubsync();

//        log->debug("Sending message on topic {} with size {} ({}) with chksm {}", topic_name, full_message.size(), message.size(), msg.checksum);

        auto priority = networked_topics[topic_name];
        mq_tx->send(full_message.data(), full_message.size(), priority);
    }

    // Helper function to queue a received message into the corresponding topic
    void topic_receive(std::string topic_name, std::vector<char>& message) {
        if (!sb->topic_exists(topic_name)) {
            return;
        }

        sb->get_topic(topic_name).deserialize_and_put(message);
    }

    void stop() override {
        threadloop::stop();
        if (server) {
            message_queue::remove("illixr_tx");
            message_queue::remove("illixr_rx");
        }
    }

    skip_option _p_should_skip() override {
        return threadloop::_p_should_skip();
    }

    void _p_one_iteration() override {
        std::vector<char> buffer(MAX_MESSAGE_SIZE);
        std::size_t       received_size;
        unsigned int      priority;
        mq_rx->receive(buffer.data(), buffer.size(), received_size, priority);

        // deserialize using Boost
        message_t msg;
        boost::iostreams::basic_array_source<char> device{buffer.data(), received_size};
        boost::iostreams::stream<boost::iostreams::basic_array_source<char>> stream{device};
        boost::archive::binary_iarchive ia{stream};
        ia >> msg;

        auto checksum = calculate_checksum(msg.message);
        if (msg.checksum != checksum) {
            log->error("Checksum mismatch on topic {}", msg.topic_name);
            return;
        }

        auto topic_name = msg.topic_name;
        auto message    = msg.message;
        auto message_size = message.size();
        auto timestamp = msg.timestamp;

        auto latency = std::chrono::high_resolution_clock::now() - timestamp;
//        log->debug("Received message on topic {} with size {} ({}) with chksm {} with latency (ms) {}", topic_name, received_size, message_size, msg.checksum, std::chrono::duration_cast<std::chrono::milliseconds>(latency).count());

//        log->debug("Received message on topic {} with size {} ({}) with chksm {}", topic_name, received_size, message_size, msg.checksum);

        topic_receive(topic_name, message);
    }

private:
    std::shared_ptr<switchboard> sb;

    bool                           server = false;
    std::shared_ptr<message_queue> mq_tx;
    std::shared_ptr<message_queue> mq_rx;

    std::map<std::string, topic_config::priority_type> networked_topics;
    std::shared_ptr<spdlog::logger>                    log;
};

extern "C" plugin* this_plugin_factory(phonebook* pb) {
    auto plugin_ptr = std::make_shared<shared_memory_network_backend>("shared_memory_network_backend", pb);
    pb->register_impl<network_backend>(plugin_ptr);
    auto* obj = plugin_ptr.get();
    return obj;
}