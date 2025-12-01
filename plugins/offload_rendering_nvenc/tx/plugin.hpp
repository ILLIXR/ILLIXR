#pragma once

#include "illixr/network/net_config.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"
#include "nvenc_encoder.hpp"

#if __has_include("rendered_frame.pb.h")
    #include "rendered_frame.pb.h"
#else
    #include "../proto/rendered_frame_stub.hpp"
#endif

#define WIDTH  2064
#define HEIGHT 2208

// #define USE_COMPRESSION

namespace ILLIXR {
class rendered_frame_tx : public threadloop {
public:
    [[maybe_unused]] rendered_frame_tx(const std::string& name, phonebook* pb);

    skip_option _p_should_skip() override;

protected:
    void _p_one_iteration() override;

private:
    void compress_frame(const rendered_frame_proto::Frame& frame);

    const std::shared_ptr<switchboard>                                    switchboard_;
    const std::shared_ptr<relative_clock>                                 clock_;
    const std::shared_ptr<stoplight>                                      stoplight_;
    switchboard::buffered_reader<switchboard::event_wrapper<std::string>> frame_reader_;
    // switchboard::network_writer<switchboard::event_wrapper<std::string>>  encoded_writer_;

    std::mutex                     mutex_;
    std::unique_ptr<nvenc_encoder> left_encoder_  = nullptr;
    std::unique_ptr<nvenc_encoder> right_encoder_ = nullptr;

    const std::string delimiter_ = "END!";
    int count = 0;
};

} // namespace ILLIXR
