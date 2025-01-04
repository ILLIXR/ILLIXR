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
    cv::Mat                     rgb0;
    cv::Mat                     rgb1;
    cv::Mat                     depth;

    rgb_depth_type(time_point _time, cv::Mat _rgb0, cv::Mat _rgb1, cv::Mat _depth)
        : time{_time}
        , rgb0{std::move(_rgb0)}
        , rgb1{std::move(_rgb1)}
        , depth{std::move(_depth)} { }
};

struct pose_rgb_depth_type : public switchboard::event {
    [[maybe_unused]] time_point time;
    cv::Mat                     rgb0;
    cv::Mat                     rgb1;
    cv::Mat                     depth;
    Eigen::Vector3f             position;
    Eigen::Quaternionf          orientation;
    std::size_t                 index;

    pose_rgb_depth_type(time_point _time, cv::Mat _rgb0, cv::Mat _rgb1, cv::Mat _depth)
        : time{_time}
        , rgb0{std::move(_rgb0)}
        , rgb1{std::move(_rgb1)}
        , depth{std::move(_depth)}
        , position{Eigen::Vector3f{0, 0, 0}}
        , orientation{Eigen::Quaternionf{1, 0, 0, 0}}
        , index{0} { }

    pose_rgb_depth_type(time_point _time, cv::Mat _rgb0, cv::Mat _rgb1, cv::Mat _depth, Eigen::Vector3f position_, Eigen::Quaternionf orientation_, std::size_t index_)
        : time{_time}
        , rgb0{std::move(_rgb0)}
        , rgb1{std::move(_rgb1)}
        , depth{std::move(_depth)}
        , position{std::move(position_)}
        , orientation{std::move(orientation_)}
        , index{index_} { }
};

} // namespace ILLIXR