#pragma once

#include "illixr/data_format/imu.hpp"
#include "illixr/data_format/opencv_data_types.hpp"
#include "illixr/network/tcpsocket.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/stoplight.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"
#include "video_encoder.hpp"

// if the header exists, we are good; if not generate a stub class for IDEs to reduce on-screen errors
#if __has_include("vio_input.pb.h")
    #include "vio_input.pb.h"
#else
    #include "../proto/input_stub.hpp"
#endif

#include <boost/lockfree/spsc_queue.hpp>

namespace ILLIXR {
class offload_writer : public threadloop {
public:
    [[maybe_unused]] offload_writer(const std::string& name, phonebook* pb);
    void start() override;
    void send_imu_cam_data(std::optional<time_point>& cam_time);
    void prepare_imu_cam_data(switchboard::ptr<const data_format::imu_type> datum);

protected:
    void _p_thread_setup() override { }

    void _p_one_iteration() override;

private:
    boost::lockfree::spsc_queue<uint64_t> queue_{1000};
    std::vector<int32_t>                  sizes_;
    std::mutex                            mutex_;
    std::condition_variable               condition_var_;
    GstMapInfo                            img0_{};
    GstMapInfo                            img1_{};
    bool                                  img_ready_ = false;

    std::unique_ptr<vio_video_encoder>                                   encoder_ = nullptr;
    std::optional<time_point>                                            latest_imu_time_;
    std::optional<time_point>                                            latest_cam_time_;
    int                                                                  frame_id_    = 0;
    vio_input_proto::IMUCamVec*                                          data_buffer_ = new vio_input_proto::IMUCamVec();
    const std::shared_ptr<switchboard>                                   switchboard_;
    const std::shared_ptr<relative_clock>                                clock_;
    const std::shared_ptr<stoplight>                                     stoplight_;
    switchboard::buffered_reader<data_format::binocular_cam_type>        cam_;
    switchboard::network_writer<switchboard::event_wrapper<std::string>> imu_cam_writer_;
    std::shared_ptr<spdlog::logger>                                      log_;
    network::TCPSocket                                                   socket_;
    std::string                                                          server_ip_;
};
} // namespace ILLIXR
