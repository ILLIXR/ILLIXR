/** @file head_pose.hpp
 * @brief Definitions of head poses.
 */
#pragma once

#include "illixr/data_format/poses/pose_base.hpp"
#include "illixr/switchboard.hpp"

#ifdef USING_OPENXR
    #include "openxr_defines.hpp"
#else // USING_OPENXR
    #if __has_include(<Eigen/Dense>)
        #include <Eigen/Dense>
    #else // __has_include(<Eigen/Dense>)
        #include <eigen3/Eigen/Dense>
    #endif // __has_include(<Eigen/Dense>)
#endif     // USING_OPENXR
#include <map>

namespace ILLIXR::data_format::pose {

/**
 * @brief Pose data for the head / HMD.
 *
 * Extends @c pose_base with no additional fields; the named type exists to
 * distinguish head-tracked poses from hand or interaction poses at the type
 * level and to provide a stable target for forward declarations.
 */
#ifdef USING_OPENXR
typedef POSE_DATA_TYPE head_pose_data;
    #define TIME_POINT int64_t
#else
    #define TIME_POINT time_point

struct head_pose_data : public pose_base {
    /**
     * @brief Default constructor. Produces an invalid, zero-translation, identity-rotation pose.
     */
    head_pose_data() = default;

    /**
     * Create an instance based on the given data
     * @param position_ The positional part of the pose
     * @param orientation_ RThe rotational part of the pose
     * @param valid_ The validity of the pose, default is true
     * @param frm The reference frame, default is RIGHT_HANDED_Y_UP
     * @param ref The reference space, default is VIEWER
     * @param confidence_ The confidence of the pose (0..1, where 0 means no confidence)
     */
    head_pose_data(Eigen::Vector3f position_, Eigen::Quaternionf orientation_, bool valid_ = true, const float confidence_ = 0.)
        : pose_base{std::move(position_), std::move(orientation_), confidence_, valid_} { }
};
#endif
#ifdef USING_OPENXR
/**
 * @typedef head_pose_type
 *
 * Pose and velocity data for the head / HMD. Using @c xrt_space_relation to be Monado compatable
 */
typedef xrt_space_relation head_pose_type;

#else
/**
 * @brief Pose and velocity data for the head / HMD, with a timestamp.
 *
 * Extends @c head_pose_data with linear and angular velocity reported by the
 * OpenXR runtime via @c XrSpaceVelocity.  Each velocity component carries its
 * own validity flag because the runtime may provide one without the other.
 *
 * Velocities are expressed in the same reference space as the pose (local space
 * by convention in this plugin) and in SI units:
 *   - @c linear_velocity  — metres per second
 *   - @c angular_velocity — radians per second (axis-angle, right-hand rule)
 */
struct [[maybe_unused]] head_pose_type
    : public switchboard::event
    , public head_pose_data {
    time_point      sensor_time;            //!< Recorded time of sensor data ingestion
    Eigen::Vector3f linear_velocity;        //!< Linear velocity in m/s; zero if unavailable
    Eigen::Vector3f angular_velocity;       //!< Angular velocity in rad/s; zero if unavailable
    bool            linear_velocity_valid;  //!< True when the runtime supplied a valid linear velocity
    bool            angular_velocity_valid; //!< True when the runtime supplied a valid angular velocity

    /**
     * @brief Default constructor. Pose invalid; velocities zero and invalid.
     */
    head_pose_type()
        : head_pose_data{}
        , sensor_time{time_point{}}
        , linear_velocity{Eigen::Vector3f::Zero()}
        , angular_velocity{Eigen::Vector3f::Zero()}
        , linear_velocity_valid{false}
        , angular_velocity_valid{false} { }

    /**
     * @brief Construct from explicit components (mutable reference overload).
     * @param sensor_time_           Timestamp associated with this data
     * @param position_              Positional part of the pose
     * @param orientation_           Rotational part of the pose
     * @param linear_velocity_       Linear velocity in m/s, defaults to zero
     * @param angular_velocity_      Angular velocity in rad/s, defaults to zero
     * @param linear_velocity_valid_ Whether linear_velocity_ contains valid data
     * @param angular_velocity_valid_ Whether angular_velocity_ contains valid data
     * @param valid_                 Pose validity, defaults to true
     * @param confidence_            Confidence in [0, 1], defaults to 0
     */
    head_pose_type(time_point sensor_time_, Eigen::Vector3f& position_, Eigen::Quaternionf& orientation_,
                   Eigen::Vector3f linear_velocity_  = Eigen::Vector3f::Zero(),
                   Eigen::Vector3f angular_velocity_ = Eigen::Vector3f::Zero(), bool linear_velocity_valid_ = false,
                   bool angular_velocity_valid_ = false, bool valid_ = true, const float confidence_ = 0.)
        : head_pose_data{position_, orientation_, valid_, confidence_}
        , sensor_time{sensor_time_}
        , linear_velocity{std::move(linear_velocity_)}
        , angular_velocity{std::move(angular_velocity_)}
        , linear_velocity_valid{linear_velocity_valid_}
        , angular_velocity_valid{angular_velocity_valid_} { }

    /**
     * @brief Construct from explicit components (const reference overload).
     * @param sensor_time_           Timestamp associated with this data
     * @param position_              Positional part of the pose
     * @param orientation_           Rotational part of the pose
     * @param linear_velocity_       Linear velocity in m/s, defaults to zero
     * @param angular_velocity_      Angular velocity in rad/s, defaults to zero
     * @param linear_velocity_valid_ Whether linear_velocity_ contains valid data
     * @param angular_velocity_valid_ Whether angular_velocity_ contains valid data
     * @param valid_                 Pose validity, defaults to true
     * @param confidence_            Confidence in [0, 1], defaults to 0
     */
    head_pose_type(time_point sensor_time_, const Eigen::Vector3f& position_, const Eigen::Quaternionf& orientation_,
                   Eigen::Vector3f linear_velocity_  = Eigen::Vector3f::Zero(),
                   Eigen::Vector3f angular_velocity_ = Eigen::Vector3f::Zero(), bool linear_velocity_valid_ = false,
                   bool angular_velocity_valid_ = false, bool valid_ = true, const float confidence_ = 0.)
        : head_pose_data{position_, orientation_, valid_, confidence_}
        , sensor_time{sensor_time_}
        , linear_velocity{std::move(linear_velocity_)}
        , angular_velocity{std::move(angular_velocity_)}
        , linear_velocity_valid{linear_velocity_valid_}
        , angular_velocity_valid{angular_velocity_valid_} { }

    /**
     * @brief Construct from a @c head_pose_data with no velocity information.
     * @param sensor_time_ Timestamp associated with the pose
     * @param other        Pose to copy; velocities will be zero and invalid
     */
    head_pose_type(time_point sensor_time_, head_pose_data& other)
        : head_pose_data{other.position, other.orientation, other.valid, other.confidence}
        , sensor_time{sensor_time_}
        , linear_velocity{Eigen::Vector3f::Zero()}
        , angular_velocity{Eigen::Vector3f::Zero()}
        , linear_velocity_valid{false}
        , angular_velocity_valid{false} { }
};
#endif

[[maybe_unused]] typedef std::map<side, head_pose_type> head_pose_map;

/**
 * Fast pose
 */
struct fast_head_pose_type : public switchboard::event {
    head_pose_type pose;                  //!< The pose data
    time_point     predict_computed_time; //!< Time at which the prediction was computed
    TIME_POINT     predict_target_time;   //!< Time that prediction targeted.

    /**
     * Basic constructor
     */
    fast_head_pose_type()
        : pose{}
        , predict_computed_time{time_point{}}
#ifdef USING_OPENXR
        , predict_target_time{0} {}
#else
        , predict_target_time{time_point{}} {
    }
#endif

        /**
         * Construct an instance using the given data
         * @param pose_ The pose to use
         * @param predict_computed_time_ The computed time
         * @param predict_target_time_ The target time
         */
        fast_head_pose_type(head_pose_type pose_, time_point predict_computed_time_, TIME_POINT predict_target_time_)
#ifdef USING_OPENXR
        : pose{pose_}
#else
        : pose{std::move(pose_)}
#endif
        , predict_computed_time{predict_computed_time_}
        , predict_target_time{predict_target_time_} {
    }

#ifdef USING_OPENXR
    [[nodiscard]] bool is_valid() const {
    #ifdef ENABLE_MONADO
        return (pose.relation_flags & XRT_SPACE_RELATION_POSITION_VALID_BIT) != 0u &&
            (pose.relation_flags & XRT_SPACE_RELATION_ORIENTATION_VALID_BIT) != 0u;
    #else
        return (pose.relation_flags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0u &&
            (pose.relation_flags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0u;
    #endif
    }
#else
    [[nodiscard]] bool is_valid() const {
        return pose.valid;
    }
#endif
};

/**
 * @brief Head pose paired with a rendered texture, used for asynchronous reprojection.
 *
 * Carries both the pose at which a frame was rendered and the rendered image
 * so that downstream stages can re-project the image to a more current pose.
 */
struct [[maybe_unused]] texture_pose : public switchboard::event {
    duration           offload_duration{};
    unsigned char*     image{};
    time_point         pose_time{};
    Eigen::Vector3f    position;
    Eigen::Quaternionf latest_quaternion;
    Eigen::Quaternionf render_quaternion;

    texture_pose() = default;

    /**
     * @brief Construct from explicit components.
     * @param offload_duration_    Time taken to offload / transfer the texture
     * @param image_               Pointer to the rendered image data
     * @param pose_time_           Timestamp of the pose used for rendering
     * @param position_            Translation at render time
     * @param latest_quaternion_   Most recent orientation at time of submission
     * @param render_quaternion_   Orientation used when the frame was rendered
     */

    texture_pose(duration offload_duration_, unsigned char* image_, time_point pose_time_, Eigen::Vector3f position_,
                 Eigen::Quaternionf latest_quaternion_, Eigen::Quaternionf render_quaternion_)
        : offload_duration{offload_duration_}
        , image{image_}
        , pose_time{pose_time_}
        , position{std::move(position_)}
        , latest_quaternion{std::move(latest_quaternion_)}
        , render_quaternion{std::move(render_quaternion_)} { }
};

} // namespace ILLIXR::data_format::pose
