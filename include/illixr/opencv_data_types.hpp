#pragma once

#include "switchboard.hpp"

#include <opencv2/core/mat.hpp>

namespace ILLIXR {

struct cam_type : switchboard::event {
    time_point time;
    cv::Mat    img0;
    cv::Mat    img1;

    cam_type(time_point _time, cv::Mat _img0, cv::Mat _img1)
        : time{_time}
        , img0{std::move(_img0)}
        , img1{std::move(_img1)} { }
};

struct rgb_depth_type : public switchboard::event {
    [[maybe_unused]] time_point time;
    cv::Mat                     rgb;
    cv::Mat                     depth;

    rgb_depth_type(time_point _time, cv::Mat _rgb, cv::Mat _depth)
        : time{_time}
        , rgb{std::move(_rgb)}
        , depth{std::move(_depth)} { }
};

} // namespace ILLIXR