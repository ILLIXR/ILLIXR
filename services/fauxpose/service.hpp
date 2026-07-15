#pragma once

#include "illixr/data_format/pose.hpp"
#include "illixr/data_format/pose_prediction.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/plugin.hpp"

namespace ILLIXR {
/// Create a "pose_prediction" type service
class MY_EXPORT_API fauxpose_impl : public data_format::pose_prediction {
public:
    explicit fauxpose_impl(const phonebook* pb);
    ~fauxpose_impl() override;

    data_format::pose_type get_true_pose() const override {
        throw std::logic_error{"Not Implemented"};
    }

    bool fast_pose_reliable() const override {
        return true;
    }

    bool true_pose_reliable() const override {
        return false;
    }

    data_format::pose_type      correct_pose(const data_format::pose_type& pose) const override;
    Eigen::Quaternionf          get_offset() override;
    void                        set_offset(const Eigen::Quaternionf& raw_o_times_offset) override;
    data_format::fast_pose_type get_fast_pose() const override;
    // ********************************************************************
    // get_fast_pose(): returns a "fast_pose_type" with the algorithmically
    //   determined location values.  (Presently moving in a circle, but
    //   always facing "front".)
    //
    // NOTE: time_type == std::chrono::system_clock::time_point
    data_format::fast_pose_type get_fast_pose(time_point time) const override;

private:
    const std::shared_ptr<switchboard>                          switchboard_;
    const std::shared_ptr<const relative_clock>                 clock_;
    switchboard::reader<switchboard::event_wrapper<time_point>> vsync_estimate_;
    mutable Eigen::Quaternionf                                  offset_{Eigen::Quaternionf::Identity()};
    mutable std::shared_mutex                                   offset_mutex_;

    time_point sim_start_time_{}; /* Store the initial time to calculate a known runtime */

    // Parameters
    double          period_;          /* The period of the circular movement (in seconds) */
    double          amplitude_;       /* The amplitude of the circular movement (in meters) */
    Eigen::Vector3f center_location_; /* The location around which the tracking should orbit */
};

class fauxpose : public plugin {
public:
    // ********************************************************************
    /* Constructor: Provide handles to faux_pose */
    [[maybe_unused]] fauxpose(const std::string& name, phonebook* pb);
    ~fauxpose() override;
};
} // namespace ILLIXR
