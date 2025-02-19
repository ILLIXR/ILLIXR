#pragma once

#include "illixr/data_format/imu.hpp"
#include "illixr/data_format/misc.hpp"
#include "illixr/data_format/opencv_data_types.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/relative_clock.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"

#include <depthai/depthai.hpp>

namespace ILLIXR {
class depthai : public plugin {
public:
    [[maybe_unused]] depthai(const std::string& name, phonebook* pb);
    void callback();

    ~depthai() override;

private:
    dai::Pipeline create_camera_pipeline() const;

    const std::shared_ptr<switchboard>                   switchboard_;
    const std::shared_ptr<const relative_clock>          clock_;
    switchboard::writer<data_format::imu_type>           imu_writer_;
    switchboard::writer<data_format::binocular_cam_type> cam_writer_;
    switchboard::writer<data_format::rgb_depth_type>     rgb_depth_;
    std::mutex                                           mutex_;

    int                                                                                     imu_packet_{0};
    int                                                                                     imu_pub_{0};
    int                                                                                     rgbd_pub_{0};
    int                                                                                     rgb_count_{0};
    int                                                                                     left_count_{0};
    int                                                                                     right_count_{0};
    int                                                                                     depth_count_{0};
    int                                                                                     all_count_{0};
    std::chrono::time_point<std::chrono::steady_clock>                                      first_packet_time_;
    std::chrono::time_point<std::chrono::steady_clock, std::chrono::steady_clock::duration> test_time_point_;
    bool                                                                                    use_raw_ = false;
    dai::Device                                                                             device_;

    std::shared_ptr<dai::DataOutputQueue> color_queue_;
    std::shared_ptr<dai::DataOutputQueue> depth_queue_;
    std::shared_ptr<dai::DataOutputQueue> rectif_left_queue_;
    std::shared_ptr<dai::DataOutputQueue> rectif_right_queue_;
    std::shared_ptr<dai::DataOutputQueue> imu_queue_;

    std::optional<ullong>     first_imu_time_;
    std::optional<time_point> first_real_imu_time_;

    std::optional<ullong>     first_cam_time_;
    std::optional<time_point> first_real_cam_time_;
};
} // namespace ILLIXR
