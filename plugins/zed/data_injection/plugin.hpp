#pragma once

#include "illixr/data_format/camera_data.hpp"
#include "illixr/data_format/hand_tracking_data.hpp"
#include "illixr/data_format/imu.hpp"
#include "illixr/data_format/opencv_data_types.hpp"
#include "illixr/data_format/pose.hpp"
#include "illixr/threadloop.hpp"

namespace ILLIXR {

class data_injection : public threadloop {
public:
    [[maybe_unused]] data_injection(const std::string& name_, phonebook* pb_);

    void _p_one_iteration() override;
    ~data_injection() override;
    void start() override;

private:
    void read_cam_data();
    void read_poses();
    void load_images_on_the_fly();

    const std::shared_ptr<switchboard>                   switchboard_;
    switchboard::writer<data_format::binocular_cam_type> frame_img_writer_;
    switchboard::writer<data_format::pose_type>          frame_pose_writer_;
    switchboard::writer<data_format::camera_data>        camera_data_writer_;

    std::map<data_format::image::image_type, cv::Mat> images_;
    std::map<uint64_t, data_format::pose_data*>       poses_;
    std::vector<uint64_t>                             timepoints_;
    data_format::camera_data                          camera_data_;
    std::string                                       data_root_path_;
    uint64_t                                          counter_;
    uint64_t                                          current_;
    uint64_t                                          step_;
    uint64_t                                          offset_;
    uint64_t                                          base_time_;
};

} // namespace ILLIXR
