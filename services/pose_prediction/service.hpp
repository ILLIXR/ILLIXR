#pragma once

#include "illixr/data_format/imu.hpp"
#include "illixr/data_format/pose.hpp"
#include "illixr/data_format/pose_prediction.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/plugin.hpp"
#include <chrono>
#include <eigen3/Eigen/Dense>
#include <fstream>
#include <iostream>
#include <sstream>

namespace ILLIXR {
class pose_prediction_impl : public data_format::pose_prediction {
public:
    explicit pose_prediction_impl(const phonebook* const pb);
    data_format::fast_pose_type get_fast_pose() const override;
    data_format::pose_type      get_true_pose() const override;
    data_format::fast_pose_type get_fast_pose(time_point future_timestamp) const override;
    void                        set_offset(const Eigen::Quaternionf& raw_o_times_offset) override;
    Eigen::Quaternionf          apply_offset(const Eigen::Quaternionf& orientation) const;
    bool                        fast_pose_reliable() const override;
    bool                        true_pose_reliable() const override;
    Eigen::Quaternionf          get_offset() override;
    data_format::pose_type      correct_pose(const data_format::pose_type& pose) const override;

    data_format::fast_pose_type get_fake_render_pose() override;
    data_format::fast_pose_type get_fake_warp_pose() override;

private:
    mutable std::atomic<bool>                                        first_time_{true};
    const std::shared_ptr<switchboard>                               switchboard_;
    const std::shared_ptr<const relative_clock>                      clock_;
    switchboard::reader<data_format::pose_type>                      slow_pose_;
    switchboard::reader<data_format::imu_raw_type>                   imu_raw_;
    switchboard::reader<data_format::pose_type>                      true_pose_;
    switchboard::reader<switchboard::event_wrapper<Eigen::Vector3f>> ground_truth_offset_;
    switchboard::reader<switchboard::event_wrapper<time_point>>      vsync_estimate_;
    mutable Eigen::Quaternionf                                       offset_{Eigen::Quaternionf::Identity()};
    mutable std::shared_mutex                                        offset_mutex_;
    const bool                                                       using_lighthouse_;

    std::vector<data_format::fast_pose_type> render_poses_;
    std::vector<data_format::fast_pose_type> warp_poses_;

    inline std::string next_token(std::istringstream& ss) {
        std::string tok;
        std::getline(ss, tok, ',');
        if (tok.empty()) {
            spdlog::get("illixr")->warn("Empty token found in pose file, please check the format.");
            ILLIXR::abort("Empty token in pose file");
        }
        return tok;
    }

    void setup_pose_reader() {
        std::string pose_path_ = switchboard_->get_env_char("ILLIXR_POSE_PATH") ? switchboard_->get_env_char("ILLIXR_POSE_PATH") : "";
        if (pose_path_.empty()) {
            spdlog::get("illixr")->error("Please set ILLIXR_POSE_PATH environment variable to the path of the pose file.");
            // ILLIXR::abort("ILLIXR_POSE_PATH is not set");
            return;
        }
        std::ifstream pose_file_(pose_path_);
        if (!pose_file_.is_open()) {
            spdlog::get("illixr")->error("Could not open pose file at {}", pose_path_);
            ILLIXR::abort("Could not open pose file");
        }
        // Read the file line by line and parse the poses
        std::string line;
        while (std::getline(pose_file_, line)) {
            std::istringstream iss(line);

            data_format::fast_pose_type render_pose{}, warp_pose{};
            uint64_t now = std::stoull(next_token(iss));

            render_pose.pose.cam_time            = time_point{std::chrono::nanoseconds(std::stoull(next_token(iss)))};
            render_pose.pose.imu_time            = time_point{std::chrono::nanoseconds(std::stoull(next_token(iss)))};
            render_pose.predict_target_time      = time_point{std::chrono::nanoseconds(std::stoull(next_token(iss)))};

            warp_pose.pose.cam_time              = time_point{std::chrono::nanoseconds(std::stoull(next_token(iss)))};
            warp_pose.pose.imu_time              = time_point{std::chrono::nanoseconds(std::stoull(next_token(iss)))};
            warp_pose.predict_target_time        = time_point{std::chrono::nanoseconds(std::stoull(next_token(iss)))};

            render_pose.pose.position.x()        = std::stof(next_token(iss));
            render_pose.pose.position.y()        = std::stof(next_token(iss));
            render_pose.pose.position.z()        = std::stof(next_token(iss));

            render_pose.pose.orientation.w()     = std::stof(next_token(iss));
            render_pose.pose.orientation.x()     = std::stof(next_token(iss));
            render_pose.pose.orientation.y()     = std::stof(next_token(iss));
            render_pose.pose.orientation.z()     = std::stof(next_token(iss));

            warp_pose.pose.position.x()          = std::stof(next_token(iss));
            warp_pose.pose.position.y()          = std::stof(next_token(iss));
            warp_pose.pose.position.z()          = std::stof(next_token(iss));

            warp_pose.pose.orientation.w()       = std::stof(next_token(iss));
            warp_pose.pose.orientation.x()       = std::stof(next_token(iss));
            warp_pose.pose.orientation.y()       = std::stof(next_token(iss));
            warp_pose.pose.orientation.z()       = std::stof(next_token(iss));

            render_poses_.emplace_back(render_pose);
            warp_poses_.emplace_back(warp_pose);
        }
    }

};

} // namespace ILLIXR
