#pragma once

#include "illixr/data_format/imu.hpp"
#include "illixr/data_format/misc.hpp"
#include "illixr/data_format/opencv_data_types.hpp"
#include "illixr/network/tcpsocket.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"
#include "video_decoder.hpp"
// if the header exists, we are good; if not generate a stub class for IDEs to reduce on-screen errors
#if __has_include("vio_input.pb.h")
    #include "vio_input.pb.h"
#else
    #include "../proto/input_stub.hpp"
#endif

#include <boost/lockfree/spsc_queue.hpp>

namespace ILLIXR {
class server_reader : public threadloop {
public:
    [[maybe_unused]] server_reader(const std::string& name, phonebook* pb);
    skip_option _p_should_skip() override;
    void        _p_one_iteration() override;
    void        start() override;

private:
    void receive_vio_input(const vio_input_proto::IMUCamVec& vio_input);

    std::unique_ptr<vio_video_decoder> decoder_;

    boost::lockfree::spsc_queue<uint64_t> queue_{1000};
    std::mutex                            mutex_;
    std::condition_variable               condition_variable_;
    cv::Mat                               img0_dst_;
    cv::Mat                               img1_dst_;
    bool                                  img_ready_ = false;

    const std::shared_ptr<switchboard>                                    switchboard_;
    const std::shared_ptr<relative_clock>                                 clock_;
    switchboard::writer<data_format::imu_type>                            imu_;
    switchboard::writer<data_format::binocular_cam_type>                  cam_;
    switchboard::buffered_reader<switchboard::event_wrapper<std::string>> imu_cam_reader_;
    std::string                                                           buffer_str_;
    std::shared_ptr<spdlog::logger>                                       log_;

    const std::string delimiter_ = "EEND!";
};
} // namespace ILLIXR
