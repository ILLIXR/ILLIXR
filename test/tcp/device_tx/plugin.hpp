#pragma once

#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"

#if __has_include("input.pb.h")
    #include "input.pb.h"
#else
    #include "../proto/input_stub.hpp"
#endif

#include <random>

namespace ILLIXR {

class MY_EXPORT_API tcp_device_tx : public threadloop {
public:
    [[maybe_unused]] tcp_device_tx(const std::string& name_, phonebook* pb_);
    skip_option _p_should_skip() override;
    void        _p_one_iteration() override;
    void        send_data();

private:
    const std::shared_ptr<switchboard>                                   switchboard_;
    const std::shared_ptr<relative_clock>                                clock_;
    const std::shared_ptr<stoplight>                                     stoplight_;
    int                                                                  frame_id_ = 0;
    switchboard::network_writer<switchboard::event_wrapper<std::string>> writer_;
    std::random_device                                                   rd_;
    std::mt19937                                                         generator_;
    const std::string                                                    delimiter_ = "EEND!";
};
} // namespace ILLIXR