#include "illixr/network/network_backend.hpp"
#include "illixr/switchboard.hpp"

#include <spdlog/spdlog.h>
#include <sstream>
#include <vector>

using namespace ILLIXR;
using namespace ILLIXR::network;

void network_backend::set_latency(const std::shared_ptr<ILLIXR::switchboard>& sb, const std::string topic_name,
                                  const std::string& transport) {
    std::string var_name = topic_name;
    std::transform(var_name.begin(), var_name.end(), var_name.begin(), ::toupper);
    std::replace(var_name.begin(), var_name.end(), ' ', '_');

    std::string latency_string = sb->get_env(var_name + "_LATENCY", "");
    if (latency_string.empty()) {
        latency_string = sb->get_env(transport + "_LATENCY", "");
        if (latency_string.empty()) {
            latency_string = sb->get_env("ILLIXR_LATENCY", "");
            if (latency_string.empty()) {
                set_default(topic_name);
                return;
            }
        }
    }
    std::stringstream ss(latency_string);
    std::string token;
    std::vector<std::string> items;

    while (std::getline(ss, token, ',')) {
        items.push_back(token);
    }
    if (items.empty()) {
        spdlog::get("illixr")->warn("Incomplete latency values for {}, none given.", latency_string);
        set_default(topic_name);
        return;
    } else if(items.size() == 1) {
        try {
            float val = std::stof(items[0]);
            latency_map_[topic_name] = std::make_shared<network::latency_constant>(val);
        } catch (...) {
            std::transform(items[0].begin(), items[0].end(), items[0].begin(), ::toupper);
            if (items[0] == "NONE") {
                set_default(topic_name);
                return;
            }
            set_default(topic_name, true, latency_string);
            return;
        }
    } else if (items.size() == 2) {
        try {
            float start = std::stof(items[0]);
            float end;
            try {
                end = std::stof(items[1]);
                latency_map_[topic_name] = std::make_shared<network::latency_random>(start, end);
                return;
            } catch (...) {
                set_default(topic_name, true, latency_string);
                return;
            }
        } catch (...) {
            set_default(topic_name, true, latency_string);
            return;
        }
    } else {
        set_default(topic_name, true, latency_string);
    }
}

void network_backend::set_default(const std::string& topic_name, bool warn, const std::string val) {
    if (warn)
        spdlog::get("illixr")->warn("Incomplete latency values for {}, it must be either \"NONE\" or one or two numbers", val);
    spdlog::get("illixr")->debug("Setting no latency for {}", topic_name);
    latency_map_[topic_name] = std::make_shared<network::latency_none>();
}
