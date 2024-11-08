#pragma once

#ifdef USE_ZED
#include <utility>

#include "data_format.hpp"
#include "opencv_data_types.hpp"

namespace ILLIXR {

typedef std::map<int, pose_type> multi_pose_map;

struct [[maybe_unused]] cam_type_zed : cam_base_type {
    std::size_t    serial_no;
    multi_pose_map poses;
    cam_type_zed(time_point _time, cv::Mat _img0, cv::Mat _img1, cv::Mat _rgb, cv::Mat _depth, cv::Mat _confidence,
                 std::size_t _serial_no, multi_pose_map _poses)
        : cam_base_type(_time,
                        {{image::LEFT, _img0},
                         {image::RIGHT, _img1},
                         {image::RGB, _rgb},
                         {image::DEPTH, _depth},
                         {image::CONFIDENCE, _confidence}},
                        image::ZED)
        , serial_no{_serial_no}
        , poses{std::move(_poses)} {}
};

}
#endif
