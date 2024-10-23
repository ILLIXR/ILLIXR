#pragma once

#include "illixr/data_format.hpp"
#include "illixr/network/tcpsocket.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"
#include "vio_output.pb.h"

namespace ILLIXR {
class offload_reader : public threadloop {
public:
    [[maybe_unused]] offload_reader(const std::string& name, phonebook* pb);
    skip_option _p_should_skip() override;
    void        _p_one_iteration() override;

private:
    void receive_vio_output(const vio_output_proto::VIOOutput& vio_output, const std::string& str_data);

    const std::shared_ptr<switchboard>        switchboard_;
    const std::shared_ptr<relative_clock>     clock_;
    switchboard::writer<pose_type>            pose_;
    switchboard::writer<imu_integrator_input> imu_integrator_input_;

    TCPSocket   socket_;
    bool        is_socket_connected_;
    std::string server_ip_;
    int         server_port_;
    std::string buffer_str_;
};
} // namespace ILLIXR