#pragma once

#include "illixr/opencv_data_types.hpp"
#include "illixr/threadloop.hpp"

#include <opencv4/opencv2/videoio.hpp>

namespace ILLIXR {
class webcam : public threadloop {
public:
    [[maybe_unused]] webcam(const std::string& name_, phonebook* pb_);
    void _p_one_iteration() override;

private:
    const std::shared_ptr<switchboard> _switchboard;
    switchboard::writer<monocular_cam_type>    _frame_pub;
    cv::VideoCapture _capture;
    bool _load_video;
    cv::Mat last_image;
    size_t last_send;
    double fps = 30.;
    int count = 0;
};

}