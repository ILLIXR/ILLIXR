#pragma once

#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"

#if __has_include("input.pb.h")
    #include "input.pb.h"
#else
    #include "../proto/input_stub.hpp"
#endif

namespace ILLIXR {

class MY_EXPORT_API tcp_server_rx : public threadloop {
public:
    [[maybe_unused]] tcp_server_rx(const std::string& name_, phonebook* pb_);
    skip_option _p_should_skip() override;
    void        _p_one_iteration() override;

private:
    void receive_message(const input_proto::Vec3& vec);

    const std::shared_ptr<switchboard>                                    switchboard_;
    const std::shared_ptr<relative_clock>                                 clock_;
    switchboard::buffered_reader<switchboard::event_wrapper<std::string>> reader_;
    std::string                                                           buffer_str_;
    unsigned int                                                          frame_count_ = 0;
    const std::string                                                     delimiter_   = "EEND!";
};
} // namespace ILLIXR