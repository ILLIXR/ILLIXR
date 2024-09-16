#pragma once

#include "data_loading.hpp"

#include "illixr/plugin.hpp"
#include "illixr/global_module_defs.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/pose_prediction.hpp"

namespace ILLIXR {
class pose_lookup_impl : public pose_prediction {
public:
    explicit pose_lookup_impl(const phonebook* const pb);
    fast_pose_type get_fast_pose() const override;
    pose_type get_true_pose() const override;
    bool fast_pose_reliable() const override;
    bool true_pose_reliable() const override;
    Eigen::Quaternionf get_offset() override;
    pose_type correct_pose(const pose_type& pose) const override;
    void set_offset(const Eigen::Quaternionf& raw_o_times_offset) override;
    Eigen::Quaternionf apply_offset(const Eigen::Quaternionf& orientation) const;
    fast_pose_type get_fast_pose(time_point time) const override;
private:
    const std::shared_ptr<switchboard>          switchboard_;
    const std::shared_ptr<const relative_clock> clock_;
    mutable Eigen::Quaternionf                  offset_{Eigen::Quaternionf::Identity()};
    mutable std::shared_mutex                   offset_mutex_;

    const std::map<ullong, sensor_types>                        sensor_data_;
    std::map<ullong, sensor_types>::const_iterator              sensor_data_it_;
    ullong                                                      dataset_first_time_;
    switchboard::reader<switchboard::event_wrapper<time_point>> vsync_estimate_;

    bool            enable_alignment_;
    Eigen::Vector3f init_pos_offset_;
    Eigen::Matrix3f align_rot_;
    Eigen::Vector3f align_trans_;
    Eigen::Vector4f align_quat_;
    double          align_scale_;
};

}