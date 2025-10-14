#include "plugin.hpp"

#include "illixr/network/topic_config.hpp"
#include "illixr/network/topic_config.hpp"

#include <algorithm>
#include <string>


using namespace ILLIXR;

[[maybe_unused]] tcp_server_tx::tcp_server_tx(const std::string& name_, phonebook* pb_)
    : threadloop{name_, pb_}
    , switchboard_{phonebook_->lookup_impl<switchboard>()}
    , clock_{phonebook_->lookup_impl<relative_clock>()}
    , stoplight_{phonebook_->lookup_impl<stoplight>()}
    , writer_{switchboard_->get_network_writer<switchboard::event_wrapper<std::string>>(
          "server_tx",
          network::topic_config{network::topic_config::priority_type::MEDIUM, false, false, network::topic_config::packetization_type::DEFAULT,
                                {}, network::topic_config::SerializationMethod::PROTOBUF})}
    , generator_{rd_()} {
    spdlog::get("illixr")->debug("Device Tx started");
}

threadloop::skip_option tcp_server_tx::_p_should_skip() {
    return skip_option::run;
}

void tcp_server_tx::_p_one_iteration() {
    if (!stoplight_->check_should_stop()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        send_message();
    }
}

void tcp_server_tx::send_message() {
    std::uniform_real_distribution<> distribution(-100., 100.);
    std::string                     message;

    auto* mvmt = new output_proto::Movement();

    auto* rot = new output_proto::Rot();
    rot->set_theta(distribution(generator_));
    rot->set_rho(distribution(generator_));

    auto* quat = new output_proto::Quat();
    quat->set_w(distribution(generator_));
    quat->set_x(distribution(generator_));
    quat->set_y(distribution(generator_));
    quat->set_z(distribution(generator_));

    mvmt->set_allocated_rotation(rot);
    mvmt->set_allocated_quat(quat);

    message = mvmt->SerializeAsString();
    message += delimiter_;
    spdlog::get("illixr")->info("Server Sending Message {} {} {} {} {} {} {}", frame_id_, mvmt->rotation().theta(),
                                mvmt->rotation().rho(), mvmt->quat().w(), mvmt->quat().x(), mvmt->quat().y(), mvmt->quat().z());
    writer_.put(std::make_shared<switchboard::event_wrapper<std::string>>(message));
    delete mvmt;
    frame_id_++;
}

PLUGIN_MAIN(tcp_server_tx)
