#include "plugin.hpp"

#include <algorithm>
#include <string>

using namespace ILLIXR;

tcp_device_tx::tcp_device_tx(const std::string& name_, phonebook* pb_)
    : threadloop{name_, pb_}
    , switchboard_{phonebook_->lookup_impl<switchboard>()}
    , clock_{phonebook_->lookup_impl<relative_clock>()}
    , stoplight_{phonebook_->lookup_impl<stoplight>()}
    , writer_{switchboard_->get_network_writer<switchboard::event_wrapper<std::string>>(
          "device_tx",
          network::topic_config{network::topic_config::priority_type::MEDIUM,
                                false,
                                false,
                                network::topic_config::packetization_type::DEFAULT,
                                {},
                                network::topic_config::SerializationMethod::PROTOBUF})}
    , generator_{rd_()} {
    spdlog::get("illixr")->debug("Device Tx started");
}

threadloop::skip_option tcp_device_tx::_p_should_skip() {
    return skip_option::run;
}

void tcp_device_tx::_p_one_iteration() {
    if (!stoplight_->check_should_stop()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        send_data();
    }
}

void tcp_device_tx::send_data() {
    std::uniform_real_distribution<> distribution(-10., 10.);
    std::string                      message;

    auto* vec = new input_proto::Vec3();

    vec->set_x(distribution(generator_));
    vec->set_y(distribution(generator_));
    vec->set_z(distribution(generator_));

    message = vec->SerializeAsString();
    message += delimiter_;
    spdlog::get("illixr")->info("Device Sending Message {} {} {} {}", frame_id_, vec->x(), vec->y(), vec->z());
    writer_.put(std::make_shared<switchboard::event_wrapper<std::string>>(message));
    delete vec;
    frame_id_++;
}

PLUGIN_MAIN(tcp_device_tx)
