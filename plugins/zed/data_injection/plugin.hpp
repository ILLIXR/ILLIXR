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
    const std::shared_ptr<switchboard>                   _switchboard;
    switchboard::writer<data_format::binocular_cam_type> _frame_img_writer;
    switchboard::writer<data_format::pose_type>          _frame_pose_writer;
    switchboard::writer<data_format::camera_data>        _camera_data_writer;

    std::map<data_format::image::image_type, cv::Mat> _images;
    std::map<uint64_t, data_format::pose_data*>       _poses;
    std::vector<uint64_t>                             _timepoints;
    data_format::camera_data                          _camera_data;
    std::string                                       _data_root_path;
    void                                              read_cam_data();
    void                                              read_poses();
    void                                              load_images_on_the_fly();
    uint64_t                                          _counter;
    uint64_t                                          _current;
    uint64_t                                          _step;
    uint64_t                                          _offset;
    uint64_t                                          _base_time;
};

} // namespace ILLIXR
