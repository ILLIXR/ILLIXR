#pragma once

#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"

#if __has_include("output.pb.h")
    #include "output.pb.h"
#else
    #include "../proto/output_stub.hpp"
#endif

namespace ILLIXR {

class tcp_device_rx : public threadloop {
public:
    [[maybe_unused]] tcp_device_rx(const std::string& name_, phonebook* pb_);
    skip_option _p_should_skip() override;
    void        _p_one_iteration() override;

private:
    void receive_message(const output_proto::Movement& mvmt);

    const std::shared_ptr<switchboard>                                    switchboard_;
    const std::shared_ptr<relative_clock>                                 clock_;
    switchboard::buffered_reader<switchboard::event_wrapper<std::string>> msg_reader_;

    unsigned int      current_frame_ = 0;
    std::string       buffer_;
    const std::string delimiter_ = "EEND!";
};
} // namespace ILLIXR