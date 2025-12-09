#pragma once

#include "../utils/device_to_server_base.hpp"
#include "illixr/data_format/scene_reconstruction.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"

#if __has_include("sr_input.pb.h")
    #include "sr_input.pb.h"
#else
    #include "../proto/input_stub.hpp"
#endif
#ifdef USE_NVIDIA_CODEC
    #include "video_decoder.hpp"
    #define DECODER_TYPE ada_video_decoder
#else
    #include "../utils/decode_utils.hpp"
    #define DECODER_TYPE encoding::rgb_decoder
#endif

#include <filesystem>

namespace ILLIXR {
const std::string delimiter = "EEND!";

class server_rx
    : public threadloop
    , public device_to_server_base {
public:
    [[maybe_unused]] server_rx(const std::string& name_, phonebook* pb_);

    skip_option _p_should_skip() override {
        return skip_option::run;
    }

    void _p_one_iteration() override;

    ~server_rx() override {
        receive_time.flush();
        receive_time.close();
        receive_size.flush();
        receive_size.close();
        receive_timestamp.flush();
        receive_timestamp.close();
    }

    void start() override;

private:
    void receive_sr_input(const sr_input_proto::SRSendData& sr_input);

    const std::shared_ptr<switchboard>                                    switchboard_;
    switchboard::writer<data_format::scene_recon_type>                    scannet_;
    switchboard::buffered_reader<switchboard::event_wrapper<std::string>> ada_reader_;

    unsigned          cur_frame;
    const std::string data_path = std::filesystem::current_path().string() + "/recorded_data";
    std::ofstream     receive_time;
    std::ofstream     receive_timestamp;
    std::ofstream     receive_size;

    std::unique_ptr<DECODER_TYPE> decoder_;

    cv::Mat img0_dst_;
    cv::Mat img1_dst_;

    // preallocated
    cv::Mat msb_buf_; // CV_8UC1, 480x640
    cv::Mat lsb_buf_; // CV_8UC1, 480x640
    cv::Mat hi16_;    // CV_16U,  480x640
    cv::Mat lo16_;    // CV_16U,  480x640
    cv::Mat depth16_; // CV_16U,  480x640

    std::vector<uint8_t> rx_buf_;
    unsigned int         current_frame_no_ = 0;
};

} // namespace ILLIXR
