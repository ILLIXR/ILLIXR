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

    pose_data()
        : position{0., 0., 0.}
        , orientation{1., 0., 0., 0.}
        , confidence{0}
        , unit{units::UNSET}
        , co_frame{coordinates::RIGHT_HANDED_Y_UP}
        , ref_space{coordinates::VIEWER}
        , valid{false} { }

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

/*
 * struct of a pose_data along with a timestamp
 */
struct [[maybe_unused]] pose_type
    : public switchboard::event
    , public pose_data {
    time_point sensor_time; //!< Recorded time of sensor data ingestion

    pose_type()
        : pose_data()
        , sensor_time{time_point{}} { }

    pose_type(time_point sensor_time_, Eigen::Vector3f& position_, Eigen::Quaternionf& orientation_,
              units::measurement_unit unit_ = units::UNSET, coordinates::frame frm = coordinates::RIGHT_HANDED_Y_UP,
              coordinates::reference_space ref = coordinates::VIEWER, const float confidence_ = 0.)
        : pose_data{position_, orientation_, unit_, frm, ref, confidence_}
        , sensor_time{sensor_time_} { }

    pose_type(time_point sensor_time_, const Eigen::Vector3f& position_, const Eigen::Quaternionf& orientation_,
              units::measurement_unit unit_ = units::UNSET, coordinates::frame frm = coordinates::RIGHT_HANDED_Y_UP,
              coordinates::reference_space ref = coordinates::VIEWER, const float confidence_ = 0.)
        : pose_data{position_, orientation_, unit_, frm, ref, confidence_}
        , sensor_time{sensor_time_} { }

    pose_type(time_point sensor_time_, pose_data& other)
        : pose_data{other.position, other.orientation, other.unit, other.co_frame, other.ref_space, other.confidence}
        , sensor_time{sensor_time_} { }
};

typedef struct {
    pose_type  pose;
    time_point predict_computed_time; // Time at which the prediction was computed
    time_point predict_target_time;   // Time that prediction targeted.
} fast_pose_type;

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
/*
 * The struct below is utilized when working with OpenXR. The internal variables are in a basic form since OpenXR uses
 * C, rather than C++ (e.g. pose contains just floats, instead of Eigen::Vector and Eigen::Quaternion)
 */

struct raw_pose {
    float x;
    float y;
    float z;
    float w;
    float wx;
    float wy;
    float wz;
    bool  valid;

    raw_pose()
        : x{0.f}
        , y{0.f}
        , z{0.f}
        , w{0.f}
        , wx{0.f}
        , wy{0.f}
        , wz{0.f}
        , valid{false} { }

    raw_pose(const pose_data& pd) {
        copy(pd);
    }

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
