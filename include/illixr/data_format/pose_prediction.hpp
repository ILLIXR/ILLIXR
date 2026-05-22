#pragma once

#include "illixr/data_format/poses/head_pose.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/relative_clock.hpp"

#ifdef __ANDROID__
#  include <Eigen/Geometry>
#else
#  include <eigen3/Eigen/Geometry>
#endif

#ifdef USING_OPENXR
#  ifdef ENABLE_MONADO
#    include "xrt/xrt_defines.h"
#    define POSE_TYPE xrt_space_relation
#  else
#    include "illixr/data_format/poses/openxr_defines.hpp"
#    define POSE_TYPE pose::xrt_space_relation
#  endif
#  include <openxr/openxr.h>
#  define POSE_TIME_TYPE XrTime
#else
#  define POSE_TIME_TYPE time_point
#  define POSE_TYPE      data_format::pose::fast_head_pose_type
#endif

namespace ILLIXR::data_format {

class pose_prediction : public phonebook::service {
public:
    [[nodiscard]] virtual POSE_TYPE get_fast_pose() const = 0;
#ifndef USING_OPENXR
    [[nodiscard]] virtual pose::head_pose_type get_true_pose() const = 0;
#endif
    [[nodiscard]] virtual POSE_TYPE get_fast_pose(POSE_TIME_TYPE future_time) const = 0;

    [[nodiscard]] virtual bool                                fast_pose_reliable() const                           = 0;
    [[nodiscard]] virtual bool                                true_pose_reliable() const                           = 0;
    virtual void                                              set_offset(const Eigen::Quaternionf& orientation)    = 0;
    [[maybe_unused]] [[nodiscard]] virtual Eigen::Quaternionf get_offset()                                         = 0;
    [[nodiscard]] virtual pose::head_pose_type                correct_pose(const pose::head_pose_type& pose) const = 0;

    ~pose_prediction() override = default;

protected:
    [[nodiscard]] pose::head_pose_type _correct_pose(const pose::head_pose_type& pose) const {
        return correct_pose(pose);
    }

    void _set_offset(const Eigen::Quaternionf& orientation) {
        set_offset(orientation);
    }
};
} // namespace ILLIXR::data_format
