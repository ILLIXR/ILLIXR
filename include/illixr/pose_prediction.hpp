#pragma once

#include "data_format.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/relative_clock.hpp"

#include <eigen3/Eigen/Geometry>

using namespace ILLIXR;

class pose_prediction : public phonebook::service {
public:
    [[nodiscard]] virtual fast_pose_type     get_fast_pose() const                             = 0;
    [[nodiscard]] virtual pose_type          get_true_pose() const                             = 0;
    [[nodiscard]] virtual fast_pose_type     get_fast_pose(time_point future_time) const       = 0;
    [[nodiscard]] virtual bool               fast_pose_reliable() const                        = 0;
    [[nodiscard]] virtual bool               true_pose_reliable() const                        = 0;
    virtual void                             set_offset(const Eigen::Quaternionf& orientation) = 0;
    [[nodiscard]] virtual Eigen::Quaternionf get_offset()                                      = 0;
    [[nodiscard]] virtual pose_type          correct_pose(const pose_type& pose) const         = 0;

    ~pose_prediction() override = default;
};
