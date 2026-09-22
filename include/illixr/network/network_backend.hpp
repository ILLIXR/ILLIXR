#pragma once

#include "illixr/phonebook.hpp"
#include "latency_injector.hpp"
#include "topic_config.hpp"

#include <map>
#include <memory>
#include <vector>

namespace ILLIXR {
class switchboard;
}

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

#ifndef __ANDROID__
    virtual void start_client() = 0;

    virtual void start_server() = 0;
#endif
    [[maybe_unused]] [[nodiscard]] virtual network::topic_config::TransportMethod transport_method() const = 0;

    [[maybe_unused]] [[nodiscard]] float get_latency(const std::string& topic_name) const {
        return static_cast<float>(latency_map_.at(topic_name)->get_latency());
    }

    void set_latency(const std::shared_ptr<ILLIXR::switchboard>& sb, const std::string topic_name,
                     const std::string& transport);

    std::map<std::string, std::shared_ptr<latency_injector>> latency_map_;

private:
    void set_default(const std::string& topic_name, bool warn = false, const std::string val = "");
};

/**
 * @brief Phonebook registration key for the TCP network backend.
 *
 * Plugins that want TCP transport register themselves under this tag.
 */
struct tcp_backend : public network_backend { };

/**
 * @brief Phonebook registration key for the UDP network backend.
 *
 * Plugins that want UDP transport register themselves under this tag.
 */
struct udp_backend : public network_backend { };

} // namespace ILLIXR::network
