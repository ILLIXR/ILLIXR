#pragma once

#include "illixr/switchboard.hpp"
#include "opencv_data_types.hpp"
#include "point.hpp"
#include "shape.hpp"
#include "template.hpp"

#include <eigen3/Eigen/Dense>
#include <iomanip>
#include <map>
#include <opencv4/opencv2/core/mat.hpp>
#include <sstream>
#include <utility>

namespace ILLIXR::data_format {
namespace ht {
    const int NUM_LANDMARKS = 21;

    /*
     * Enum for the landmark points on a hand
     */
    enum landmark_points : int {
        WRIST,             // = 0,
        THUMB_CMC,         // = 1,
        THUMB_MCP,         // = 2,
        THUMB_IP,          // = 3,
        THUMB_TIP,         // = 4,
        INDEX_FINGER_MCP,  // = 5,
        INDEX_FINGER_PIP,  // = 6,
        INDEX_FINGER_DIP,  // = 7,
        INDEX_FINGER_TIP,  // = 8,
        MIDDLE_FINGER_MCP, // = 9,
        MIDDLE_FINGER_PIP, // = 10,
        MIDDLE_FINGER_DIP, // = 11,
        MIDDLE_FINGER_TIP, // = 12,
        RING_FINGER_MCP,   // = 13,
        RING_FINGER_PIP,   // = 14,
        RING_FINGER_DIP,   // = 15,
        RING_FINGER_TIP,   // = 16,
        PINKY_MCP,         // = 17,
        PINKY_PIP,         // = 18,
        PINKY_DIP,         // = 19,
        PINKY_TIP,         // = 20
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
                                                   {PINKY_TIP, "Pinky Tip"}};

    enum hand : int { LEFT_HAND = 0, RIGHT_HAND = 1 };

    const std::vector<hand> hand_map{LEFT_HAND, RIGHT_HAND};

    struct hand_points : points_with_units {
        hand_points(units::measurement_unit unit_ = units::UNSET)
            : points_with_units(NUM_LANDMARKS, unit_) { }

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

        void flip_y(const uint im_height = 0) {
            if (unit == units::PERCENT) {
                for (auto& pnt : points) {
                    if (pnt.y() -= 0.)
                        pnt.y() = 1.0 - pnt.y();
                }
                return;
            }
            if (im_height == 0)
                throw std::runtime_error("Cannot rectify point with non-precent units with no height given.");
            for (auto& pnt : points) {
                if (pnt.y() != 0.)
                    pnt.y() = (float) im_height - pnt.y();
            }
        }
    };

    struct position {
        std::map<ht::hand, ht::hand_points> points;
        units::measurement_unit             unit;
        uint64_t                            time;
        bool                                valid = false;

        position() { }

        position(const std::map<ht::hand, ht::hand_points>& pnts, units::measurement_unit unit_, uint64_t time_)
            : points{pnts}
            , unit{unit_}
            , time{time_}
            , valid{true} { }
    };

    struct velocity : hand_points {
        velocity()
            : hand_points() { }

        velocity(units::measurement_unit unit)
            : hand_points(unit) { }

        velocity(const hand_points& h1, const hand_points& h2, const float time)
            : hand_points() {
            if (h1.unit != h2.unit) {
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
        size_t               proc_time; //!< nanoseconds of processing time
        std::map<hand, rect> palms;     //!< left palm detection
        std::map<hand, rect> hands;     //!< left hand detection

        std::map<hand, float> confidence;

        std::map<hand, hand_points> points;

        ht_detection() { }

        ht_detection(size_t ptime, rect* lp, rect* rp, rect* lh, rect* rh, float lc, float rc, hand_points* lhp,
                     hand_points* rhp)
            : proc_time{ptime}
            , palms{{LEFT_HAND, (lp) ? *lp : rect()}, {RIGHT_HAND, (rp) ? *rp : rect()}}
            , hands{{LEFT_HAND, (lh) ? *lh : rect()}, {RIGHT_HAND, (rh) ? *rh : rect()}}
            , confidence{{LEFT_HAND, lc}, {RIGHT_HAND, rc}}
            , points{{LEFT_HAND, (lhp) ? *lhp : hand_points()}, {RIGHT_HAND, (rhp) ? *rhp : hand_points()}} { }
    };

    struct ht_frame : cam_base_type {
        std::map<units::eyes, ht_detection> detections;
        std::map<ht::hand, hand_points>     hand_positions;
        std::map<ht::hand, velocity>        hand_velocities;
        pose_data                           wcs_offset;
        coordinates::reference_space        reference;
        units::measurement_unit             unit;

        ht_frame(time_point time, std::map<image::image_type, cv::Mat> images, std::map<units::eyes, ht_detection> _detections,
                 std::map<ht::hand, hand_points> points, std::map<ht::hand, velocity> velocities = {}, pose_data _pose = {},
                 coordinates::reference_space ref_sp = coordinates::VIEWER,
                 units::measurement_unit      _unit  = units::measurement_unit::UNSET)
            : cam_base_type(time, std::move(images), (images.size() == 2) ? camera::BINOCULAR : camera::MONOCULAR)
            , detections(std::move(_detections))
            , hand_positions{std::move(points)}
            , hand_velocities{std::move(velocities)}
            , wcs_offset{std::move(_pose)}
            , reference{ref_sp}
            , unit{_unit} {
            if (wcs_offset.valid) {
                unit = wcs_offset.unit;
            } else if (hand_positions.count(LEFT_HAND) > 0 && hand_positions.at(LEFT_HAND).valid) {
                unit = hand_positions.at(LEFT_HAND).unit;
            } else if (hand_positions.count(RIGHT_HAND) > 0 && hand_positions.at(RIGHT_HAND).valid) {
                unit = hand_positions.at(RIGHT_HAND).unit;
            } else {
                unit = units::UNSET;
            }
        }
    };

    // #ifdef BUILD_OXR
    struct raw_ht_data {
        uint64_t                     time;
        raw_point                    h_points[2][NUM_LANDMARKS];
        raw_point                    h_velocities[2][NUM_LANDMARKS];
        raw_pose                     wcs_origin;
        coordinates::reference_space reference   = coordinates::VIEWER;
        units::measurement_unit      unit        = units::measurement_unit::UNSET;
        coordinates::frame           frame       = coordinates::RIGHT_HANDED_Y_UP;
        bool                         hp_valid[2] = {false, false};
        bool                         hv_valid[2] = {false, false};
        bool                         valid       = false;

        raw_ht_data()
            : time{0} { }

        raw_ht_data(const time_point time, const std::map<ht::hand, hand_points>& points,
                    const std::map<ht::hand, velocity>& velocities, const pose_data& pose,
                    coordinates::reference_space ref_sp = coordinates::VIEWER,
                    units::measurement_unit      _unit  = units::measurement_unit::UNSET)
            : time{static_cast<uint64_t>(time.time_since_epoch().count())}
            , reference{ref_sp}
            , unit{_unit}
            , valid{true} {
            for (auto i = 0; i < NUM_LANDMARKS; i++) {
                h_points[LEFT_HAND][i].copy(points.at(LEFT_HAND).at(i));
                hp_valid[LEFT_HAND] |= points.at(LEFT_HAND).at(i).valid;
                h_points[RIGHT_HAND][i].copy(points.at(RIGHT_HAND).at(i));
                hp_valid[RIGHT_HAND] |= points.at(RIGHT_HAND).at(i).valid;
                h_velocities[LEFT_HAND][i].copy(velocities.at(LEFT_HAND).at(i));
                hv_valid[LEFT_HAND] |= velocities.at(LEFT_HAND).at(i).valid;
                h_velocities[RIGHT_HAND][i].copy(velocities.at(RIGHT_HAND).at(i));
                hv_valid[RIGHT_HAND] |= velocities.at(RIGHT_HAND).at(i).valid;
            }

            wcs_origin.copy(pose);

            if (unit == units::measurement_unit::UNSET) {
                if (pose.valid) {
                    unit = pose.unit;
                } else if (points.count(LEFT_HAND) > 0 && points.at(LEFT_HAND).valid) {
                    unit = points.at(LEFT_HAND).unit;
                } else if (points.count(RIGHT_HAND) > 0 && points.at(RIGHT_HAND).valid) {
                    unit = points.at(RIGHT_HAND).unit;
                }
            }
        }

        explicit raw_ht_data(const ht_frame& frame_)
            : raw_ht_data(frame_.time, frame_.hand_positions, frame_.hand_velocities, frame_.wcs_offset, frame_.reference,
                          frame_.unit) { }

        void copy(const ht_frame& frame_) {
            time      = static_cast<uint64_t>(frame_.time.time_since_epoch().count());
            reference = frame_.reference;
            unit      = frame_.unit;

            for (auto i = 0; i < NUM_LANDMARKS; i++) {
                h_points[LEFT_HAND][i].copy(frame_.hand_positions.at(LEFT_HAND).at(i));
                hp_valid[LEFT_HAND] = hp_valid[LEFT_HAND] || frame_.hand_positions.at(LEFT_HAND).at(i).valid;
                h_points[RIGHT_HAND][i].copy(frame_.hand_positions.at(RIGHT_HAND).at(i));
                hp_valid[RIGHT_HAND] = hp_valid[RIGHT_HAND] || frame_.hand_positions.at(RIGHT_HAND).at(i).valid;
                h_velocities[LEFT_HAND][i].copy(frame_.hand_velocities.at(LEFT_HAND).at(i));
                hv_valid[LEFT_HAND] = hv_valid[LEFT_HAND] || frame_.hand_velocities.at(LEFT_HAND).at(i).valid;
                h_velocities[RIGHT_HAND][i].copy(frame_.hand_velocities.at(RIGHT_HAND).at(i));
                hv_valid[RIGHT_HAND] = hv_valid[RIGHT_HAND] || frame_.hand_velocities.at(RIGHT_HAND).at(i).valid;
            }

            wcs_origin.copy(frame_.wcs_offset);
        }
    };

    inline std::ostream& operator<<(std::ostream& os, const raw_ht_data& data) {
        const auto def_precision{os.precision()};

        os << std::setprecision(6) << "HT Data" << std::endl << "  Time: " << data.time << std::endl;
        os << "  WCS origin: ";
        if (data.wcs_origin.valid) {
            os << std::endl
               << "    position: " << data.wcs_origin.x << ", " << data.wcs_origin.y << ", " << data.wcs_origin.z << std::endl;
            os << "    orientation: " << data.wcs_origin.w << ", " << data.wcs_origin.wx << ", " << data.wcs_origin.wy << ", "
               << data.wcs_origin.z;
        } else {
            os << "not valid";
        }
        os << std::endl << "  Ref space: " << data.reference << std::endl;
        os << "  Unit" << data.unit << std::endl;
        os << "  Left Hand:";
        if (data.hp_valid[LEFT_HAND]) {
            for (auto i = 0; i < NUM_LANDMARKS; i++) {
                os << std::endl
                   << "    " << i << ": " << data.h_points[LEFT_HAND][i].x << ", " << data.h_points[LEFT_HAND][i].y << ", "
                   << data.h_points[LEFT_HAND][i].z << "  " << ((data.h_points[LEFT_HAND][i].valid) ? "valid" : "not valid");
            }
        } else {
            os << "not valid";
        }
        os << std::endl << "  Right Hand:";
        if (data.hp_valid[RIGHT_HAND]) {
            for (auto i = 0; i < NUM_LANDMARKS; i++) {
                os << std::endl
                   << "    " << i << ": " << data.h_points[RIGHT_HAND][i].x << ", " << data.h_points[RIGHT_HAND][i].y << ", "
                   << data.h_points[RIGHT_HAND][i].z << "  " << ((data.h_points[RIGHT_HAND][i].valid) ? "valid" : "not valid");
            }
        } else {
            os << "not valid";
        }
        os << std::endl << "  Left Hand (vel):";
        if (data.hv_valid[LEFT_HAND]) {
            for (auto i = 0; i < NUM_LANDMARKS; i++) {
                os << std::endl
                   << "    " << i << ": " << data.h_velocities[LEFT_HAND][i].x << ", " << data.h_velocities[LEFT_HAND][i].y
                   << ", " << data.h_velocities[LEFT_HAND][i].z << "  "
                   << ((data.h_velocities[LEFT_HAND][i].valid) ? "valid" : "not valid");
            }
        } else {
            os << "not valid";
        }
        os << std::endl << "  Right Hand (vel):";
        if (data.hv_valid[RIGHT_HAND]) {
            for (auto i = 0; i < NUM_LANDMARKS; i++) {
                os << std::endl
                   << "    " << i << ": " << data.h_velocities[RIGHT_HAND][i].x << ", " << data.h_velocities[RIGHT_HAND][i].y
                   << ", " << data.h_velocities[RIGHT_HAND][i].z << "  "
                   << ((data.h_velocities[RIGHT_HAND][i].valid) ? "valid" : "not valid");
            }
        } else {
            os << "not valid";
        }
        os << std::endl << std::endl;
        os << std::setprecision(def_precision);
        return os;
    }

    // #endif

    inline void transform_point(point_with_units& pnt, const pose_data& pose) {
        Eigen::Vector3f new_pnt = pose.orientation * pnt;
        pnt.set(new_pnt + pose.position);
    }

    inline static void transform_points(points_with_units& points, const pose_data& pose, coordinates::reference_space from,
                                        coordinates::reference_space to) {
        if (to == from)
            return;
        if (to == coordinates::WORLD) {
            for (auto& point : points.points) {
                Eigen::Vector3f newpnt = pose.orientation * point;
                point.set(newpnt + pose.position);
            }
        } else {
        }
    }

} // namespace ht

template<>
inline void normalize<ht::hand_points>(ht::hand_points& obj, const float width, const float height, const float depth) {
    if (obj.unit == units::PERCENT) {
        std::cout << "Points are already normalized";
        return;
    }
    for (auto& pnt : obj.points)
        normalize(pnt, width, height, depth);
    obj.unit = units::PERCENT;
}

template<>
inline void denormalize<ht::hand_points>(ht::hand_points& obj, const float width, const float height, const float depth,
                                         units::measurement_unit unit_) {
    for (auto& pnt : obj.points)
        denormalize(pnt, width, height, depth, unit_);
    obj.unit = unit_;
}

template<>
inline void normalize<ht::velocity>(ht::velocity& obj, const float width, const float height, const float depth) {
    if (obj.unit == units::PERCENT) {
        std::cout << "Points are already normalized";
        return;
    }
    for (auto& pnt : obj.points)
        normalize(pnt, width, height, depth);
    obj.unit = units::PERCENT;
}

template<>
inline void denormalize<ht::velocity>(ht::velocity& obj, const float width, const float height, const float depth,
                                      units::measurement_unit unit_) {
    for (auto& pnt : obj.points)
        denormalize(pnt, width, height, depth, unit_);
    obj.unit = unit_;
}

template<>
inline void normalize<ht::ht_detection>(ht::ht_detection& obj, const float width, const float height, const float depth) {
    for (auto& palm : obj.palms)
        normalize(palm.second, width, height, depth);
    for (auto& hnd : obj.hands)
        normalize(hnd.second, width, height, depth);
    for (auto& pnts : obj.points)
        normalize(pnts.second, width, height, depth);
}

template<>
inline void normalize<ht::ht_frame>(ht::ht_frame& obj, const float width, const float height, const float depth) {
    for (auto& det : obj.detections)
        normalize(det.second, width, height, depth);
    for (auto& hp : obj.hand_positions)
        normalize(hp.second, width, height, depth);
    for (auto& hv : obj.hand_velocities)
        normalize(hv.second, width, height, depth);
}

template<>
inline void denormalize<ht::ht_detection>(ht::ht_detection& obj, const float width, const float height, const float depth,
                                          units::measurement_unit unit) {
    for (auto& palm : obj.palms)
        denormalize(palm.second, width, height, depth, unit);
    for (auto& hnd : obj.hands)
        denormalize(hnd.second, width, height, depth, unit);
    for (auto& pnts : obj.points)
        denormalize(pnts.second, width, height, depth, unit);
}

template<>
inline void denormalize<ht::ht_frame>(ht::ht_frame& obj, const float width, const float height, const float depth,
                                      units::measurement_unit unit) {
    for (auto& det : obj.detections)
        denormalize(det.second, width, height, depth, unit);
    for (auto& hp : obj.hand_positions)
        denormalize(hp.second, width, height, depth, unit);
    for (auto& hv : obj.hand_velocities)
        denormalize(hv.second, width, height, depth, unit);
}
} // namespace ILLIXR::data_format
