#pragma once

#include "illixr/data_format.hpp"
#include "illixr/network/tcpsocket.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/plugin.hpp"
#include "illixr/switchboard.hpp"

namespace ILLIXR {
class server_writer : public plugin {
public:
    [[maybe_unused]] server_writer(const std::string& name, phonebook* pb);
    void start() override;
    void send_vio_output(const switchboard::ptr<const pose_type>& datum);

private:
    const std::shared_ptr<switchboard>        switchboard_;
    switchboard::reader<imu_integrator_input> imu_int_input_;
    switchboard::network_writer<switchboard::event_wrapper<std::string>> vio_pose_writer_;
};
} // namespace ILLIXR
