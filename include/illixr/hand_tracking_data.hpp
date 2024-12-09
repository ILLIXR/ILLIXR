#pragma once

#include "switchboard.hpp"
#include "opencv_data_types.hpp"
#include "data_format.hpp"

#include <eigen3/Eigen/Dense>
#include <map>
#include <sstream>
#include <opencv2/core/mat.hpp>

namespace ILLIXR {
    namespace HandTracking {
const int NUM_LANDMARKS = 21;
/*
 * Enum for the landmark points on a hand
 */
enum landmark_points : int {
    WRIST, // = 0,
    THUMB_CMC, // = 1,
    THUMB_MCP, // = 2,
    THUMB_IP, // = 3,
    THUMB_TIP, // = 4,
    INDEX_FINGER_MCP, // = 5,
    INDEX_FINGER_PIP, // = 6,
    INDEX_FINGER_DIP, // = 7,
    INDEX_FINGER_TIP, // = 8,
    MIDDLE_FINGER_MCP, // = 9,
    MIDDLE_FINGER_PIP, // = 10,
    MIDDLE_FINGER_DIP, // = 11,
    MIDDLE_FINGER_TIP, // = 12,
    RING_FINGER_MCP, // = 13,
    RING_FINGER_PIP, // = 14,
    RING_FINGER_DIP, // = 15,
    RING_FINGER_TIP, // = 16,
    PINKY_MCP, // = 17,
    PINKY_PIP, // = 18,
    PINKY_DIP, // = 19,
    PINKY_TIP, // = 20
};

/**
 * Mapping of the hand landmark points to strings, useful for display purposes
 */
const std::map<int, std::string> point_str_map{{WRIST, "Wrist"},
                                               {THUMB_CMC, "Thumb CMC"},
                                               {THUMB_MCP, "Thumb MCP"},
                                               {THUMB_IP, "Thumb IP"},
                                               {THUMB_TIP, "Thumb Tip"},
                                               {INDEX_FINGER_MCP, "Index MCP"},
                                               {INDEX_FINGER_PIP, "Index PIP"},
                                               {INDEX_FINGER_DIP, "Index DIP"},
                                               {INDEX_FINGER_TIP, "Index Tip"},
                                               {MIDDLE_FINGER_MCP, "Middle MCP"},
                                               {MIDDLE_FINGER_PIP, "Middle PIP"},
                                               {MIDDLE_FINGER_DIP, "Middle DIP"},
                                               {MIDDLE_FINGER_TIP, "Middle Tip"},
                                               {RING_FINGER_MCP, "Ring MCP"},
                                               {RING_FINGER_PIP, "Ring PIP"},
                                               {RING_FINGER_DIP, "Ring DIP"},
                                               {RING_FINGER_TIP, "Ring Tip"},
                                               {PINKY_MCP, "Pinky MCP"},
                                               {PINKY_PIP, "Pinky PIP"},
                                               {PINKY_DIP, "Pinky DIP"},
                                               {PINKY_TIP, "Pinky Tip"}
};



enum hand : int {
    LEFT_HAND = 0,
    RIGHT_HAND = 1
};
const std::vector<hand> hand_map{LEFT_HAND, RIGHT_HAND};


struct hand_points : points_with_units {
    hand_points(units::measurement_unit unit_ = units::UNSET) : points_with_units(NUM_LANDMARKS, unit_) {}
    hand_points(std::vector<point_with_validity>& pnts, units::measurement_unit unit_ = units::UNSET)
        : points_with_units(pnts, unit_) {
        check();
    }
    hand_points(std::vector<point>& pnts, units::measurement_unit unit_ = units::UNSET, bool valid_ = true)
        : points_with_units(pnts, unit_, valid_) {
        check();
    }
    void check() {
        if (points.size() < NUM_LANDMARKS) {
            std::cout << "Resizing to " << NUM_LANDMARKS << std::endl;
            points.resize(NUM_LANDMARKS);
        } else if (points.size() > NUM_LANDMARKS) {
            std::cout << "Shrinking to " << NUM_LANDMARKS << std::endl;
            points.resize(NUM_LANDMARKS);
        }
    }
    void clear() {
        points.clear();
    }

    void flip_y() {
        if (unit == units::PERCENT)
            for (auto &pnt : points)
                pnt.y() = 1.0 - pnt.y();
        else
            throw std::runtime_error("Cannot rectify point with non-precent units");
    }
};

struct position {
    std::map<HandTracking::hand, HandTracking::hand_points> points;
    ::ILLIXR::units::measurement_unit unit;
    uint64_t time;
    bool valid = false;

    position() {}
    position(const std::map<HandTracking::hand, HandTracking::hand_points>& pnts,
             ::ILLIXR::units::measurement_unit unit_,
             uint64_t time_)
             : points{pnts}
             , unit{unit_}
             , time{time_}
             , valid{true} {}
};

struct velocity : hand_points {
    velocity() : hand_points() {}

    velocity(const hand_points& h1, const hand_points& h2, const float time) : hand_points() {
        if (h1.unit != h2.unit){
            // TODO: something
        }
        if (h1.points.size() != h2.points.size()) {
            // TODO: something
        }

        for (int i = WRIST; i != PINKY_TIP; i++) {
            points[i] = (h2.at(i) - h1.at(i)) / time;
        }
        unit = h1.unit;
    }
};

/**
 * Struct containing all the data from a hand detection
 */
struct ht_detection {
    size_t proc_time;             //!< nanoseconds of processing time
    std::map<hand, ::ILLIXR::rect> palms;   //!< left palm detection
    std::map<hand, ::ILLIXR::rect> hands;   //!< left hand detection

    std::map<hand, float> confidence;

    std::map<hand, hand_points> points;
    ht_detection() {}
    ht_detection(size_t ptime, ::ILLIXR::rect* lp, ::ILLIXR::rect* rp, ::ILLIXR::rect* lh, ::ILLIXR::rect* rh, float lc, float rc, hand_points *lhp, hand_points *rhp)
        : proc_time{ptime},
        palms{{LEFT_HAND, (lp) ? *lp : ::ILLIXR::rect()}, {RIGHT_HAND, (rp) ? *rp : ::ILLIXR::rect()}},
        hands{{LEFT_HAND, (lh) ? *lh : ::ILLIXR::rect()}, {RIGHT_HAND, (rh) ? *rh : ::ILLIXR::rect()}},
        confidence{{LEFT_HAND, lc}, {RIGHT_HAND, rc}},
        points{{LEFT_HAND, (lhp) ? *lhp : hand_points()},
               {RIGHT_HAND, (rhp) ? *rhp : hand_points()}} {}
};

struct ht_frame : cam_base_type {
    std::map<units::eyes, ht_detection> detections;
    std::map<HandTracking::hand, hand_points> hand_positions;
    std::map<HandTracking::hand, velocity> hand_velocities;
    pose_data offset_pose;
    coordinates::reference_space reference;

    ht_frame(time_point _time, std::map<image::image_type, cv::Mat> _imgs,
             std::map<units::eyes, ht_detection> _detections, std::map<HandTracking::hand, hand_points> points,
             std::map<HandTracking::hand, velocity> velocities = {}, pose_data _pose = {},
             coordinates::reference_space ref_sp = coordinates::VIEWER)
        : cam_base_type(_time, std::move(_imgs), (_imgs.size() == 2) ? camera::BINOCULAR : camera::MONOCULAR)
        , detections(std::move(_detections))
        , hand_positions{points}
        , hand_velocities{velocities}
        , offset_pose{_pose}
        , reference{ref_sp} {}
};

    float center_y = 0.;        //!> optical center pixel along y-axis
    double vertical_fov = 0.;    //!> vertical field of view
    double horizontal_fov = 0.;  //!> horizontal field of view
    bool valid = false;
    camera_params(): valid{false} {}
    camera_params(const float xc, const float yc, const double vfov, const double hfov) : center_x{xc}
            , center_y{yc}
            , vertical_fov{vfov}
            , horizontal_fov{hfov}
            , valid{true} {}
};

struct camera_config {
    std::map <int, camera_params> cam_par;
    size_t width = 0;
    size_t height = 0;
    float fps = 0.;
    float baseline = 0.;
    base_unit units = UNSET;
    bool valid = false;

template<>
void normalize<HandTracking::hand_points>(HandTracking::hand_points& obj, const float width, const float height, const float depth) {
    if (obj.unit == units::PERCENT) {
        std::cout << "Points are already normalized";
        return;
    }
    for (auto& pnt : obj.points)
        ::ILLIXR::normalize(pnt, width, height, depth);
    obj.unit = units::PERCENT;
}

template<>
void denormalize<HandTracking::hand_points>(HandTracking::hand_points& obj, const float width, const float height, const float depth,
                                    units::measurement_unit unit_) {
    for (auto& pnt : obj.points)
        ::ILLIXR::denormalize(pnt, width, height, depth, unit_);
    obj.unit = unit_;
}

template<>
void normalize<HandTracking::velocity>(HandTracking::velocity& obj, const float width, const float height, const float depth) {
    if (obj.unit == units::PERCENT) {
        std::cout << "Points are already normalized";
        return;
    }
    for (auto& pnt : obj.points)
        ::ILLIXR::normalize(pnt, width, height, depth);
    obj.unit = units::PERCENT;
}

template<>
void denormalize<HandTracking::velocity>(HandTracking::velocity& obj, const float width, const float height, const float depth,
                                            units::measurement_unit unit_) {
    for (auto& pnt : obj.points)
        ::ILLIXR::denormalize(pnt, width, height, depth, unit_);
    obj.unit = unit_;
}

template<>
void normalize<HandTracking::ht_detection>(HandTracking::ht_detection& obj, const float width, const float height,
                                           const float depth) {
    for (auto& palm : obj.palms)
        normalize(palm.second, width, height, depth);
    for (auto& hnd : obj.hands)
        normalize(hnd.second, width, height, depth);
    for (auto& pnts : obj.points)
        normalize(pnts.second, width, height, depth);
}

template<>
void normalize<HandTracking::ht_frame>(HandTracking::ht_frame& obj, const float width, const float height,
                                       const float depth) {
    for (auto& det : obj.detections)
        normalize(det.second, width, height, depth);
    for (auto& hp : obj.hand_positions)
        normalize(hp.second, width, height, depth);
    for (auto& hv : obj.hand_velocities)
        normalize(hv.second, width, height, depth);
}

template<>
void denormalize<HandTracking::ht_detection>(HandTracking::ht_detection& obj, const float width, const float height,
                                             const float depth, units::measurement_unit unit) {
    for (auto& palm : obj.palms)
        denormalize(palm.second, width, height, depth, unit);
    for (auto& hnd : obj.hands)
        denormalize(hnd.second, width, height, depth, unit);
    for (auto& pnts : obj.points)
        denormalize(pnts.second, width, height, depth, unit);
}

template<>
void denormalize<HandTracking::ht_frame>(HandTracking::ht_frame& obj, const float width, const float height,
                                         const float depth, units::measurement_unit unit) {
    for (auto& det : obj.detections)
        denormalize(det.second, width, height, depth, unit);
    for (auto& hp : obj.hand_positions)
        denormalize(hp.second, width, height, depth, unit);
    for (auto& hv : obj.hand_velocities)
        denormalize(hv.second, width, height, depth, unit);
}
}  // namespace: ILLIXR
