#pragma once

#include "illixr/data_format.hpp"
#include "illixr/opencv_data_types.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"
#include "illixr/network/socket.hpp"

#include "video_decoder.hpp"
#include "vio_input.pb.h"


#include <boost/lockfree/spsc_queue.hpp>

namespace ILLIXR {
class server_reader : public threadloop {
public:
    [[maybe_unused]] server_reader(const std::string& name, phonebook* pb);
    skip_option _p_should_skip() override;
    void _p_one_iteration() override;
    ~server_reader() override;
    void start() override;
private:
    void receive_vio_input(const vio_input_proto::IMUCamVec& vio_input);

    std::unique_ptr<video_decoder> decoder_;

    boost::lockfree::spsc_queue<uint64_t> queue_{1000};
    std::mutex                            mutex_;
    std::condition_variable               condition_variable_;
    cv::Mat                               img0_dst_;
    cv::Mat                               img1_dst_;
    bool                                  img_ready_ = false;

    const std::shared_ptr<switchboard>     switchboard_;
    switchboard::writer<imu_type>          imu_;
    switchboard::writer<cam_type>          cam_;
    switchboard::writer<connection_signal> conn_signal_;

    TCPSocket   socket_;
    TCPSocket*  read_socket_ = NULL;
    Address     server_addr_;
    std::string buffer_str_;

};
}