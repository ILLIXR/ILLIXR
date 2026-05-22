#pragma once

#include <map>
#ifdef USING_OPENXR
#  ifdef ENABLE_MONADO
#    include "xrt/xrt_defines.h"
#    define POSE_DATA_TYPE xrt_pose
#  else
#    include <openxr/openxr.h>
#    include "openxr_defines.hpp"
#    define POSE_DATA_TYPE XrPosef
#  endif
#else
#  if __has_include(<Eigen/Dense>)
#    include <Eigen/Dense>
#  else // __has_include(<Eigen/Dense>)
#    include <eigen3/Eigen/Dense>
#    include <utility>
#  endif // __has_include(<Eigen/Dense>)
#endif     // USING_OPENXR

namespace ILLIXR::data_format::pose {
/** @brief Distinguishes the left and right hand. */
enum side : int { LEFT = 0, RIGHT = 1 };

[[maybe_unused]] inline side non_primary(const side sd) {
    if (sd == LEFT)
        return RIGHT;
    return LEFT;
}

/**
 * @brief Base struct for all pose types in ILLIXR.
 *
 * Captures the common 6-DOF pose fields (position, orientation) together with
 * the metadata that every derived pose needs: confidence, and validity.  Derived structs should
 * inherit from this type rather than duplicating these fields.
 */
#ifdef USING_OPENXR
struct pose_base : POSE_DATA_TYPE {
#else
struct pose_base {
    Eigen::Vector3f    position;    //!< Translation component of the pose (x, y, z)
    Eigen::Quaternionf orientation; //!< Rotation component of the pose as a unit quaternion
    float              confidence;  //!< Confidence in the pose estimate in [0, 1], where 1 is highest confidence
    bool               valid;       //!< Whether this pose contains valid, usable data
#endif

#ifdef USING_OPENXR
    pose_base()
        : POSE_DATA_TYPE{} { }

    explicit pose_base(POSE_DATA_TYPE pose)
        : POSE_DATA_TYPE{pose} { }

    pose_base(const pose_base& base)
            : POSE_DATA_TYPE{} {
        position    = base.position;
        orientation = base.orientation;
    }

#else
    /**
     * @brief Default constructor. Produces an invalid, zero-translation, identity-rotation pose.
     */
    pose_base()
        : position{0.f, 0.f, 0.f}
        , orientation{1.f, 0.f, 0.f, 0.f}
        , confidence{0.f}
        , valid{false} { }

    /**
     * @brief Construct a pose from explicit components.
     * @param position_    Translation component of the pose
     * @param orientation_ Rotation component of the pose (must be unit quaternion)
     * @param confidence_  Confidence value in [0, 1], defaults to 0
     * @param valid_       Whether the pose is valid, defaults to true
     */

    pose_base(Eigen::Vector3f position_, Eigen::Quaternionf orientation_, float confidence_ = 0.f, bool valid_ = true)
        : position{std::move(position_)}
        , orientation{std::move(orientation_)}
        , confidence{confidence_}
        , valid{valid_} { }
#endif

#ifdef USING_OPENXR

    void update(POSE_DATA_TYPE pose) {
        position    = pose.position;
        orientation = pose.orientation;
    }
#else
    void update(Eigen::Vector3f position_, Eigen::Quaternionf orientation_) {
        position    = std::move(position_);
        orientation = std::move(orientation_);
    }
#endif
};

[[maybe_unused]] typedef std::map<side, pose_base> multi_pose_map;

} // namespace ILLIXR::data_format::pose
