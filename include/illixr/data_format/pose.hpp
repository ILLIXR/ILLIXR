#pragma once

#include "illixr/data_format/coordinate.hpp"
#include "illixr/data_format/unit.hpp"
#include "illixr/switchboard.hpp"

#include <eigen3/Eigen/Dense>

namespace ILLIXR::data_format {
struct [[maybe_unused]] pose_data {
    Eigen::Vector3f              position;
    Eigen::Quaternionf           orientation;
    float                        confidence;
    units::measurement_unit      unit;
    coordinates::frame           co_frame;
    coordinates::reference_space ref_space;
    bool                         valid;

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

struct [[maybe_unused]] pose_type
    : public switchboard::event
    , public pose_data {
    time_point sensor_time; // Recorded time of sensor data ingestion

    pose_type()
        : pose_data()
        , sensor_time{time_point{}} { }

    pose_type(time_point sensor_time_, Eigen::Vector3f& position_, Eigen::Quaternionf& orientation_,
              units::measurement_unit unit_ = units::UNSET, coordinates::frame frm = coordinates::RIGHT_HANDED_Y_UP,
              coordinates::reference_space ref = coordinates::VIEWER, const float confidence_ = 0.)
        : pose_data{position_, orientation_, unit_, frm, ref, confidence_}
        , sensor_time{sensor_time_} { }

    pose_type(time_point sensor_time_, const Eigen::Vector3f position_, const Eigen::Quaternionf orientation_,
              units::measurement_unit unit_ = units::UNSET, coordinates::frame frm = coordinates::RIGHT_HANDED_Y_UP,
              coordinates::reference_space ref = coordinates::VIEWER, const float confidence_ = 0.)
        : pose_data{std::move(position_), std::move(orientation_), unit_, frm, ref, confidence_}
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

typedef std::map<units::eyes, pose_type> multi_pose_map;

} // namespace ILLIXR::data_format