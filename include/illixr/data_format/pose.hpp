#pragma once

#include "illixr/data_format/coordinate.hpp"
#include "illixr/data_format/unit.hpp"
#include "illixr/switchboard.hpp"

#include <eigen3/Eigen/Dense>

namespace ILLIXR::data_format {
/**
 * struct containing basic pose data
 */
struct pose_data {
    Eigen::Vector3f         position;    //!< x, y, and z of the translation
    Eigen::Quaternionf      orientation; //!< quaternion representing the rotation of the pose from the reference frame origin
    float                   confidence;  //!< confidence rating of the pose data 0. - 1. with 1. being 100%
    units::measurement_unit unit;        //!< units for the translation portion of the pose
    coordinates::frame      co_frame;    //!< the coordinate reference frame (e.g. left handed y up)
    coordinates::reference_space
         ref_space; //!< the reference space (VIEWER = origin is camera, WORLD = origin is specified at startup of system
    bool valid;     //!< whether the pose contains valid data

    /**
     * Basic constructor
     */
    pose_data()
        : position{0., 0., 0.}
        , orientation{1., 0., 0., 0.}
        , confidence{0}
        , unit{units::UNSET}
        , co_frame{coordinates::RIGHT_HANDED_Y_UP}
        , ref_space{coordinates::VIEWER}
        , valid{false} { }

    /**
     * Create an instance based on the given data
     * @param position_ The positional part of the pose
     * @param orientation_ RThe rotational part of the pose
     * @param unit_ The units for the pose, default is UNSET
     * @param frm The reference frame, default is RIGHT_HANDED_Y_UP
     * @param ref The reference space, default is VIEWER
     * @param confidence_ The confidence of the pose (0..1, where 0 means no confidence)
     * @param valid_ The validity of the pose
     */
    pose_data(Eigen::Vector3f position_, Eigen::Quaternionf orientation_, units::measurement_unit unit_ = units::UNSET,
              coordinates::frame frm = coordinates::RIGHT_HANDED_Y_UP, coordinates::reference_space ref = coordinates::VIEWER,
              const float confidence_ = 0., bool valid_ = true)
        : position{std::move(position_)}
        , orientation{std::move(orientation_)}
        , confidence(confidence_)
        , unit{unit_}
        , co_frame{frm}
        , ref_space{ref}
        , valid{valid_} { }
};

/**
 * struct of a pose_data along with a timestamp
 */
struct [[maybe_unused]] pose_type
    : public switchboard::event
    , public pose_data {
    time_point cam_time;
    time_point imu_time;

    /**
     * Basic constructor
     */
    pose_type()
        : pose_data()
        , cam_time{time_point{}}
        , imu_time{time_point{}} { }

    /**
     * Construct an instance based on the given data
     * @param cam_time Time of the camera data associated with this pose
     * @param imu_time Time of the IMU data associated with this pose
     * @param position_ The positional part of the pose
     * @param orientation_ RThe rotational part of the pose
     * @param unit_ The units for the pose, default is UNSET
     * @param frm The reference frame, default is RIGHT_HANDED_Y_UP
     * @param ref The reference space, default is VIEWER
     * @param confidence_ The confidence of the pose (0..1, where 0 means no confidence)
     */
    pose_type(time_point cam_time_, time_point imu_time_, Eigen::Vector3f& position_, Eigen::Quaternionf& orientation_,
              units::measurement_unit unit_ = units::UNSET, coordinates::frame frm = coordinates::RIGHT_HANDED_Y_UP,
              coordinates::reference_space ref = coordinates::VIEWER, const float confidence_ = 0.)
        : pose_data{position_, orientation_, unit_, frm, ref, confidence_}
        , cam_time{cam_time_}
        , imu_time{imu_time_} { }

    /**
     * Construct an instance based on the given data
     * @param cam_time Time of the camera data associated with this pose
     * @param imu_time Time of the IMU data associated with this pose
     * @param orientation_ RThe rotational part of the pose
     * @param unit_ The units for the pose, default is UNSET
     * @param frm The reference frame, default is RIGHT_HANDED_Y_UP
     * @param ref The reference space, default is VIEWER
     * @param confidence_ The confidence of the pose (0..1, where 0 means no confidence)
     */
    pose_type(time_point cam_time_, time_point imu_time_, const Eigen::Vector3f& position_, const Eigen::Quaternionf& orientation_,
              units::measurement_unit unit_ = units::UNSET, coordinates::frame frm = coordinates::RIGHT_HANDED_Y_UP,
              coordinates::reference_space ref = coordinates::VIEWER, const float confidence_ = 0.)
        : pose_data{position_, orientation_, unit_, frm, ref, confidence_}
        , cam_time{cam_time_}
        , imu_time{imu_time_} { }

    /**
     * Construct an instance using the given pose
     * @param cam_time Time of the camera data associated with this pose
     * @param imu_time Time of the IMU data associated with this pose
     * @param other The pose to use
     */
    pose_type(time_point cam_time_, time_point imu_time_, pose_data& other)
        : pose_data{other.position, other.orientation, other.unit, other.co_frame, other.ref_space, other.confidence}
        , cam_time{cam_time_}
        , imu_time{imu_time_} { }
};

/**
 * Fast pose
 */
struct fast_pose_type : public switchboard::event {
    pose_type  pose;                  //!< The pose data
    time_point predict_computed_time; //!< Time at which the prediction was computed
    time_point predict_target_time;   //!< Time that prediction targeted.

    /**
     * Basic constructor
     */
    fast_pose_type()
        : pose{}
        , predict_computed_time{time_point{}}
        , predict_target_time{time_point{}} { }

    /**
     * Construct an instance using the given data
     * @param pose_ The pose to use
     * @param predict_computed_time_ The computed time
     * @param predict_target_time_ The target time
     */
    fast_pose_type(pose_type pose_, time_point predict_computed_time_, time_point predict_target_time_)
        : pose{std::move(pose_)}
        , predict_computed_time{predict_computed_time_}
        , predict_target_time{predict_target_time_} { }
};

struct [[maybe_unused]] texture_pose : public switchboard::event {
    duration           offload_duration{};
    unsigned char*     image{};
    time_point         pose_time{};
    Eigen::Vector3f    position;
    Eigen::Quaternionf latest_quaternion;
    Eigen::Quaternionf render_quaternion;

    texture_pose() = default;

    texture_pose(duration offload_duration_, unsigned char* image_, time_point pose_time_, Eigen::Vector3f position_,
                 Eigen::Quaternionf latest_quaternion_, Eigen::Quaternionf render_quaternion_)
        : offload_duration{offload_duration_}
        , image{image_}
        , pose_time{pose_time_}
        , position{std::move(position_)}
        , latest_quaternion{std::move(latest_quaternion_)}
        , render_quaternion{std::move(render_quaternion_)} { }
};

[[maybe_unused]] typedef std::map<units::eyes, pose_type> multi_pose_map;

#ifdef ENABLE_OXR
/**
 * This struct is utilized when working with OpenXR. The internal variables are in a basic form since OpenXR uses
 * C, rather than C++ (e.g. pose contains just floats, instead of Eigen::Vector and Eigen::Quaternion)
 */
struct raw_pose {
    float x;     //!< x-coordinate
    float y;     //!< y-coordinate
    float z;     //!< z-coordinate
    float w;     //!< quaternion w
    float wx;    //!< quaternion x
    float wy;    //!< quaternion y
    float wz;    //!< quaternion z
    bool  valid; //!< validity flag

    /**
     * Basic constructor
     */
    raw_pose()
        : x{0.f}
        , y{0.f}
        , z{0.f}
        , w{0.f}
        , wx{0.f}
        , wy{0.f}
        , wz{0.f}
        , valid{false} { }

    /**
     * Create an instance using the given pose
     * @param pd The pose to use
     */
    explicit raw_pose(const pose_data& pd) {
        copy(pd);
    }

    /**
     * Copy a pose into this structure
     * @param pd The pose to copy
     */
    void copy(const pose_data& pd) {
        x     = pd.position.x();
        y     = pd.position.y();
        z     = pd.position.z();
        w     = pd.orientation.w();
        wx    = pd.orientation.x();
        wy    = pd.orientation.y();
        wz    = pd.orientation.z();
        valid = pd.valid;
    }
};

#endif
} // namespace ILLIXR::data_format
