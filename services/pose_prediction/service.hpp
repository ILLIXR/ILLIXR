#pragma once

#include "illixr/data_format/imu.hpp"
#include "illixr/data_format/pose.hpp"
#include "illixr/data_format/pose_prediction.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/plugin.hpp"

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

    void setup_pose_reader() {
        std::string pose_path_ = switchboard_->get_env_char("ILLIXR_POSE_PATH");
        if (pose_path_.empty()) {
            spdlog::get("illixr")->error("Please set ILLIXR_POSE_PATH environment variable to the path of the pose file.");
            ILLIXR::abort("ILLIXR_POSE_PATH is not set");
        }
        std::ifstream pose_file_(pose_path_);
        if (!pose_file_.is_open()) {
            spdlog::get("illixr")->error("Could not open pose file at {}", pose_path_);
            ILLIXR::abort("Could not open pose file");
        }
        // std::string line;
        // while (std::getline(pose_file_, line)) {
        //     std::istringstream iss(line);
        //     data_format::fast_pose_type pose;
        //     iss >> pose.imu_time >> pose.position.x() >> pose.position.y() >> pose.position.z()
        //         >> pose.orientation.w() >> pose.orientation.x() >> pose.orientation.y() >>
    }

};

} // namespace ILLIXR
