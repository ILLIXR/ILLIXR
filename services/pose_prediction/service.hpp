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
};

} // namespace ILLIXR
