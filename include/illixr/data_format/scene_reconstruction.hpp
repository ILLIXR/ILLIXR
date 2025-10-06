#pragma once

#include "illixr/switchboard.hpp"
#include "pose.hpp"

#include <opencv4/opencv2/core/mat.hpp>
#include <set>

namespace ILLIXR::data_format {

struct [[maybe_unused]] scene_recon_path : public switchboard::event {
    [[maybe_unused]] time_point time;
    pose_type                   pose;
    std::string                 depth_path;
    std::string                 rgb_path;

    [[maybe_unused]] scene_recon_path(time_point camera_time, pose_type pose_, std::string depth_path_, std::string rgb_path_)
        : time{camera_time}
        , pose{pose_}
        , depth_path{depth_path_}
        , rgb_path{rgb_path_} { }
};

struct [[maybe_unused]] scene_recon_type : public switchboard::event {
    [[maybe_unused]] time_point time;
    pose_type                   pose;
    cv::Mat                     depth;
    cv::Mat                     rgb; // rgb is only you need colored mesh
    [[maybe_unused]] bool       last_frame;

    [[maybe_unused]] scene_recon_type(time_point camera_time, pose_type pose_, cv::Mat depth_, cv::Mat rgb_, bool is_last_frame)
        : time{camera_time}
        , pose{pose_}
        , depth{depth_}
        , rgb{rgb_}
        , last_frame{is_last_frame} { }
};

struct [[maybe_unused]] vb_type : public switchboard::event {
    std::set<std::tuple<int, int, int>> unique_VB_lists;
    unsigned                            scene_id;

    vb_type(std::set<std::tuple<int, int, int>>&& generated_list, unsigned id_)
        //: unique_VB_lists{generated_list}
        // 912 change to accmodate move()
        : unique_VB_lists{std::move(generated_list)}
        , scene_id{id_} { }
};

} // namespace ILLIXR::data_format
