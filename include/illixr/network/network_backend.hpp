#pragma once

#include "illixr/phonebook.hpp"
#include "topic_config.hpp"

#include <memory>
#include <vector>

namespace ILLIXR::network {
class network_backend : public phonebook::service {
public:
    /**
     * Called when a topic is created.
     *
     * The backend must maintain a list of networked topics. This adds a topic to that list.
     * @param topic_name The name of the topic.
     * @param config The configuration of the topic.
     */
    virtual void topic_create(std::string topic_name, topic_config& config) = 0;

    /**
     * Used to query if a topic is networked.
     *
     * The backend should coordinate with the other endpoints to determine if a topic is networked.
     * There are two cases where this should return true:
     *     1. topic_create has been called with the same topic_name
     *     2. topic_create has not been called with the same topic_name, but the topic is networked by another endpoint
     * The backend implementation should coordinate with the other endpoints to determine if a topic is networked.
     * @param topic_name The name of the topic.
     */
    virtual bool is_topic_networked(std::string topic_name) = 0;

    /**
     * Called when a message is requested to be sent on a topic by a plugin.
     * @param topic_name The name of the topic.
     * @param message The message to send.
     */
    virtual void topic_send(std::string topic_name, std::string&& message) = 0;
};

class local_network_backend : public phonebook::service {
public:
    /**
     * Called when a topic is created.
     *
     * The backend must maintain a list of networked topics. This adds a topic to that list.
     * @param topic_name The name of the topic.
     * @param config The configuration of the topic.
     */
    virtual void topic_create(std::string topic_name, topic_config& config) = 0;

    /**
     * Used to query if a topic is networked.
     *
     * The backend should coordinate with the other endpoints to determine if a topic is networked.
     * There are two cases where this should return true:
     *     1. topic_create has been called with the same topic_name
     *     2. topic_create has not been called with the same topic_name, but the topic is networked by another endpoint
     * The backend implementation should coordinate with the other endpoints to determine if a topic is networked.
     * @param topic_name The name of the topic.
     */
    virtual bool is_topic_networked(std::string topic_name) = 0;

    /**
     * Called when a message is requested to be sent on a topic by a plugin.
     * @param topic_name The name of the topic.
     * @param message The message to send.
     */
    virtual void topic_send(std::string topic_name, std::string&& message) = 0;
};
} // namespace ILLIXR::network
