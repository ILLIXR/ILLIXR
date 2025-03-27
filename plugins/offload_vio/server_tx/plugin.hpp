#pragma once

#include "illixr/data_format/imu.hpp"
#include "illixr/data_format/misc.hpp"
#include "illixr/data_format/pose.hpp"
#include "illixr/network/tcpsocket.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/plugin.hpp"
#include "illixr/switchboard.hpp"

namespace ILLIXR {
class server_writer : public plugin {
public:
    [[maybe_unused]] server_writer(const std::string& name, phonebook* pb);
    void start() override;
    void send_vio_output(const switchboard::ptr<const data_format::pose_type>& datum);

private:
    const std::shared_ptr<switchboard>                                   switchboard_;
    switchboard::reader<data_format::imu_integrator_input>               imu_int_input_;
    switchboard::network_writer<switchboard::event_wrapper<std::string>> vio_pose_writer_;
};
} // namespace ILLIXR
