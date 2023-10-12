#ifndef ILLIXR_NETWORK_BACKEND_HPP
#define ILLIXR_NETWORK_BACKEND_HPP

#include "illixr/plugin.hpp"
#include "topic_config.hpp"

#include <memory>
#include <vector>

namespace ILLIXR {
class network_backend : public plugin {
public:
    network_backend(const std::string& name, phonebook* pb)
        : plugin(name, pb) { }

    /**
     * Called when a topic is created.
     * @param topic_name The name of the topic.
     * @param config The configuration of the topic.
     */
    virtual void topic_create(std::string topic_name, topic_config config) = 0;

    /**
     * Used to query if a topic is networked.
     * @param topic_name The name of the topic.
     */
    virtual bool is_topic_networked(std::string topic_name) = 0;

    /**
     * Called when a message is requested to be sent on a topic by a plugin.
     * @param topic_name The name of the topic.
     * @param message The message to send.
     */
    virtual void topic_send(std::string topic_name, std::vector<char> message) = 0;

    /**
     * Retrieve a message on a topic. This DOES NOT dequeue the message.
     * If this is called multiple times, the SAME message will be returned.
     * @param topic_name The name of the topic.
     * @return The least recently received message on the topic. If no message is available, nullptr is returned.
     */
    virtual std::shared_ptr<std::vector<char>> topic_get(std::string topic_name) = 0;

    /**
     * Dequeue a message on a topic. This removes the message from the queue.
     * @param topic_name The name of the topic.
     * @return The least recently received message on the topic. If no message is available, nullptr is returned.
     */
    virtual std::shared_ptr<std::vector<char>> topic_dequeue(std::string topic_name) = 0;
};
}

#endif
