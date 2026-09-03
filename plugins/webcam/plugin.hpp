#pragma once

#include "illixr/data_format/opencv_data_types.hpp"
#include "illixr/threadloop.hpp"

#include <opencv4/opencv2/videoio.hpp>

namespace ILLIXR {
class webcam : public threadloop {
public:
    [[maybe_unused]] webcam(const std::string& name_, phonebook* pb_);
    void _p_one_iteration() override;

private:
    const std::shared_ptr<switchboard>                   switchboard_;
    switchboard::writer<data_format::monocular_cam_type> frame_pub_;
    cv::VideoCapture                                     capture_;
    bool                                                 load_video_;
};

} // namespace ILLIXR
