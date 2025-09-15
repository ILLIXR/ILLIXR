#pragma once

#include "illixr/data_format/pose_prediction.hpp"
#include "illixr/data_loading.hpp"
#include "illixr/global_module_defs.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/plugin.hpp"

namespace ILLIXR {

typedef data_format::pose_type sensor_types;

class MY_EXPORT_API pose_lookup_impl : public data_format::pose_prediction {
public:
    explicit pose_lookup_impl(const phonebook* const pb);
    data_format::fast_pose_type get_fast_pose() const override;
    data_format::pose_type      get_true_pose() const override;
    bool                        fast_pose_reliable() const override;
    bool                        true_pose_reliable() const override;
    Eigen::Quaternionf          get_offset() override;
    data_format::pose_type      correct_pose(const data_format::pose_type& pose) const override;
    void                        set_offset(const Eigen::Quaternionf& raw_o_times_offset) override;
    Eigen::Quaternionf          apply_offset(const Eigen::Quaternionf& orientation) const;
    data_format::fast_pose_type get_fast_pose(time_point time) const override;

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

} // namespace ILLIXR
