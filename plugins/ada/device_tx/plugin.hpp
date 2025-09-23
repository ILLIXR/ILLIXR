#pragma once

#include "../device_to_server_base.hpp"
#include "illixr/data_format/scene_reconstruction.hpp"
#include "illixr/network/net_config.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/plugin.hpp"
#include "illixr/stoplight.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"
#include "video_encoder.hpp"

#if __has_include("sr_input.pb.h")
    #include "sr_input.pb.h"
#else
    #include "../proto/input_stub.hpp"
#endif

#include <filesystem>
#include <string>

// #define frame_count_ 1150 //scene 0005
// #define frame_count_ 1078 //scene 0131
// #define frame_count_ 1649 //for 0568
// #define frame_count_ 925 //for 0655
// #define frame_count_ 502 //scene 0077

namespace ILLIXR {

const std::string delimiter = "EEND!";

class device_tx
    : public threadloop
    , public device_to_server_base {
public:
    [[maybe_unused]] device_tx(const std::string& name_, phonebook* pb_);

    void start() override;

    void send_scene_recon_data(switchboard::ptr<const data_format::scene_recon_type> datum);
    ~device_tx() override;

protected:
    void _p_one_iteration() override;

private:
    // pyh more cache and memory friendly, less overhead
    static void decompose_16bit_to_8bit(const cv::Mat& depth16, cv::Mat& out_msb, cv::Mat& out_lsb);

    const std::shared_ptr<switchboard>                                   switchboard_;
    const std::shared_ptr<relative_clock>                                clock_;
    const std::shared_ptr<stoplight>                                     stoplight_;
    std::unique_ptr<ada_video_encoder>                                   encoder_ = nullptr;
    switchboard::network_writer<switchboard::event_wrapper<std::string>> ada_writer_;

    const std::string data_path_ = std::filesystem::current_path().string() + "/recorded_data";
    std::ofstream     sending_timestamp_;
    std::ofstream     starting_timestamp_;
    std::ofstream     frame_send_timing_;

    unsigned                    frame_id_{0};
    sr_input_proto::SRSendData* outgoing_payload = new sr_input_proto::SRSendData();
    sr_input_proto::Pose*       pose             = nullptr;

    std::string send_buf_;
    std::string msb_bytes_;
    std::string lsb_bytes_;
};

} // namespace ILLIXR
