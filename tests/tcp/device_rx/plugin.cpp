#include "plugin.hpp"

#include <spdlog/spdlog.h>

using namespace ILLIXR;

tcp_device_rx::tcp_device_rx(const std::string& name_, phonebook* pb_)
    : threadloop{name_, pb_}
    , switchboard_{phonebook_->lookup_impl<switchboard>()}
    , clock_{phonebook_->lookup_impl<relative_clock>()}
    , msg_reader_{switchboard_->get_buffered_reader<switchboard::event_wrapper<std::string>>("server_tx")} {
    spdlog::get("illixr")->debug("Dev_rx started");
}

threadloop::skip_option tcp_device_rx::_p_should_skip() {
    return skip_option::run;
}

void tcp_device_rx::_p_one_iteration() {
    if (msg_reader_.size() > 0) {
        auto                   buffer_ptr   = msg_reader_.dequeue();
        std::string            buffer_str   = **buffer_ptr;
        std::string::size_type end_position = buffer_str.find(delimiter_);

        output_proto::Movement mvmt;
        bool                   success = mvmt.ParseFromString(buffer_str.substr(0, end_position));
        if (success) {
            receive_message(mvmt);
        } else {
            spdlog::get("illixr")->error("Cound not parse string");
        }
    }
}

void tcp_device_rx::receive_message(const output_proto::Movement& mvmt) {
    spdlog::get("illixr")->info("Device Received: {} {} {} {} {} {} {}", current_frame_, mvmt.rotation().theta(),
                                mvmt.rotation().rho(), mvmt.quat().w(), mvmt.quat().x(), mvmt.quat().y(), mvmt.quat().z());
    current_frame_++;
}

PLUGIN_MAIN(tcp_device_rx)
