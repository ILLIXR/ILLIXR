#include "plugin.hpp"

#include <spdlog/spdlog.h>

using namespace ILLIXR;

[[maybe_unused]] tcp_server_rx::tcp_server_rx(const std::string& name_, phonebook* pb_)
    : threadloop{name_, pb_}
    , switchboard_{phonebook_->lookup_impl<switchboard>()}
    , clock_{pb_->lookup_impl<relative_clock>()}
    , reader_{switchboard_->get_buffered_reader<switchboard::event_wrapper<std::string>>("device_tx")} {
    spdlog::get("illixr")->debug("Started server Rx");
}

threadloop::skip_option tcp_server_rx::_p_should_skip() {
    return skip_option::run;
}

void tcp_server_rx::_p_one_iteration() {
    if (reader_.size() > 0) {
        auto                   buffer_ptr   = reader_.dequeue();
        std::string            buffer_str   = **buffer_ptr;
        std::string::size_type end_position = buffer_str.find(delimiter_);

        input_proto::Vec3 vec;
        bool              success = vec.ParseFromString(buffer_str.substr(0, end_position));
        if (success) {
            receive_message(vec);
        } else {
            spdlog::get("illixr")->error("Cound not parse string");
        }
    }
}


void tcp_server_rx::receive_message(const input_proto::Vec3& vec) {
    spdlog::get("illixr")->info("Server Received {} {} {} {}", frame_count_, vec.x(), vec.y(), vec.z());
    frame_count_++;
}

PLUGIN_MAIN(tcp_server_rx)
