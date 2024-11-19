#pragma once

#include "switchboard.hpp"
#include "opencv_data_types.hpp"

#include <eigen3/Eigen/Dense>
#include <map>
#include <sstream>
#include <opencv2/core/mat.hpp>

namespace ILLIXR::HandTracking {
const int NUM_LANDMARKS = 21;
/*
 * Enum for the landmark points on a hand
 */
enum landmark_points {
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

typedef Eigen::Vector3f basic_point;
/*
struct basic_point {
    double x = 0.;           //!< x-coordiante
    double y = 0.;           //!< y-coordinate
    double z = 0.;           //!< z_coordinate

    basic_point() : x(0.), y(0.), z(0.) {}

    explicit basic_point(basic_point* other) {
        if (other != nullptr) {
            x = other->x;
            y = other->y;
            z = other->z;
        }
    }

    basic_point(const double _x, const double _y, const double _z): x(_x), y(_y), z(_z) {}

    void set(const double _x, const double _y, const double _z) {
        x = _x;
        y = _y;
        z = _z;
    }

    bool operator==(const basic_point& other) const {
        return (x == other.x && y == other.y && z == other.z);
    }

    basic_point operator*(const double &val) const {
        return {x * val, y * val, z * val};
    }
    basic_point operator/(const double &val) const {
        return {x / val, y / val, z / val};
    }
    basic_point operator+(const basic_point& other) const {
        return {x + other.x, y + other.y, z + other.z};
    }
    basic_point operator-(const basic_point& other) const {
        return {x - other.x, y - other.y, z - other.z};
    }

    basic_point& operator*=(const double &val) {
        x *= val;
        y *= val;
        z *= val;
        return *this;
    }

    basic_point& operator/=(const double &val) {
        x /= val;
        y /= val;
        z /= val;
        return *this;
    }

    basic_point& operator+=(const basic_point &other) {
        x += other.x;
        y += other.y;
        z += other.z;
        return *this;
    }
    basic_point& operator-=(const basic_point &other) {
        x -= other.x;
        y -= other.y;
        z -= other.z;
        return *this;
    }
    [[nodiscard]]std::string to_string() const {
        std::ostringstream buffer;
        buffer.setf(std::ios::fixed);
        buffer.precision(2);
        buffer << x << ", " << y << ", " << z;
        return buffer.str();
    }
};*/

/**
 * Struct for containing the 3D coordinates of a point
 */
struct point : basic_point{
    double cx = 0.;
    double cy = 0.;
    double cz = 0.;
    bool normalized= false;  //!< if the coordinates are normalized [0..1] (true) or pixel values (false)
    bool valid = false;      //!< if the point is valid

    /**
     * Generic constructor which sets all valies to 0.
     */
    point() : basic_point(0., 0., 0.), cx(0.), cy(0.), cz(0.), normalized(false), valid(false) {}

    /**
     * Copy constructor
     * @param other the point to initialize this one with
     */
    explicit point(point* other) {
        if (other != nullptr) {
            x() = other->x();
            y() = other->y();
            z() = other->z();
            cx = other->cx;
            cy = other->cy;
            cz = other->cz;
            normalized = other->normalized;
            valid = other->valid;
        }
    }

    /**
     * General constructor
     * @param _x the x coordinate
     * @param _y the y coordinate
     * @param _z the z coordinate
     * @param norm whether the corrdinates are normalized, or raw pixel values
     * @param _valid whether the point is valid
     */
    point(const double _x, const double _y, const double _z, bool norm = false, bool _valid = true,
          const double _cx = 0., const double _cy = 0., const double _cz = 0.) :
        basic_point(_x, _y, _z), cx(_cx), cy(_cy), cz(_cz), normalized(norm), valid(_valid) {}

    /**
     * Set the values of an already existing point
     * @param _x the x coordinate
     * @param _y the y coordinate
     * @param _z the z coordinate
     * @param norm whether the corrdinates are normalized, or raw pixel values
     */
    void set(const double _x, const double _y, const double _z, bool norm = false, const double _cx = 0.,
             const double _cy = 0., const double _cz = 0.) {
        x() = _x;
        y() = _y;
        z() = _z;
        cx = _cx;
        cy = _cy;
        cz = _cz;
        normalized = norm;
        valid = true;
    }

    /**
     * Equality comparison
     * @param other The point to compare this one to
     * @return true if they are the same
     */
    bool operator==(const point &other) const {
        return (x() == other.x() && y() == other.y() && z() == other.z() && normalized == other.normalized &&
                valid == other.valid && cx == other.cx && cy == other.cy && cz == other.cz);
    }

    /**
     * Multiplication operator for scaling the internal values by a constant
     * @param val The value to scale the point by
     * @return
     */
    point operator*(const double &val) const {
        return {x() * val, y() * val, z() * val, normalized, valid, cx * val, cy * val, cz * val};
    }
    /**
     * Division operator for scaling the internal values by a constant
     * @param val The value to scale the point by
     * @return
     */
    point operator/(const double &val) const {
        return {x() / val, y() / val, z() / val, normalized, valid, cx / val, cy / val, cz / val};
    }

    /**
     * Operator to add two points together
     * @param other The point to add to this one
     * @return a new point containing the addition operation results
     */
    point operator+(const point& other) const {
        // TODO: need to add confidence
        if(normalized != other.normalized)
            throw std::runtime_error("Invalid unit comparison");
        return {x() + other.x(), y() + other.y(), z() + other.z(), normalized, valid && other.valid};
    }
    /**
     * Operator to subtract another point from this one
     * @param other The point to subtract from this one
     * @return a new point containing the subtraction operation results
     */
    point operator-(const point& other) const {
        // TODO: need to add confidence
        if(normalized != other.normalized)
            throw std::runtime_error("Invalid unit comparison");
        return {x() - other.x(), y() - other.y(), z() - other.z(), normalized, valid && other.valid};
    }

    /**
     * In place multiplication operator for scaling the internal values by a constant
     * @param val The value to scale the point by
     * @return
     */
    point& operator*=(const double &val) {
        x() *= val;
        y() *= val;
        z() *= val;
        cx *= val;
        cy *= val;
        cz *= val;
        return *this;
    }
    /**
     * In place division operator for scaling the internal values by a constant
     * @param val The value to scale the point by
     * @return
     */
    point& operator/=(const double &val) {
        x() /= val;
        y() /= val;
        z() /= val;
        cx /= val;
        cy /= val;
        cz /= val;
        return *this;
    }

    /**
     * In place operator to add two points together
     * @param other The point to add to this one
     * @return a new point containing the addition operation results
     */
    point& operator+=(const point &other) {
        // TODO: need to add confidence
        if(normalized != other.normalized)
            throw std::runtime_error("Invalid units comparison");
        x() += other.x();
        y() += other.y();
        z() += other.z();
        valid &= other.valid;
        return *this;
    }
    /**
     * In place operator to add two points together
     * @param other The point to add to this one
     * @return a new point containing the addition operation results
     */
    point& operator-=(const point &other) {
        // TODO: need to add confidence
        if(normalized != other.normalized)
            throw std::runtime_error("Invalid units comparison");
        x() -= other.x();
        y() -= other.y();
        z() -= other.z();
        valid &= other.valid;
        return *this;
    }

    void normalize(const int wd, const int ht) {
        x() /= wd;
        y() /= ht;
    }

    void denormalize(const int wd, const int ht) {
        x() *= wd;
        y() *= ht;
    }
};

/**
 * Determine the absolute value of a point (done on each coordinate)
 * @param value The point to take the absolute value of
 * @return A point containing the result
 */
inline point abs(const point& value) {
    return {std::abs(value.x()), std::abs(value.y()), std::abs(value.z()), value.normalized, value.valid, value.cx, value.cy, value.cz};
}

typedef point velocity;

/**
 * Determine the velocity of a moving point
 * @param p1 The initial position of the point
 * @param p2 The final position of the point
 * @param step The time interval between the points
 * @return The velocity vector of the point in units/sec
 */
inline velocity movement(const point &p1, const point &p2, const duration &step) {
    if(p1.normalized != p2.normalized)
        throw std::runtime_error("Invalid unit comparison");
    velocity result = abs(p1) + abs(p2);
    result /= (static_cast<double>(step.count()) / static_cast<double>(std::chrono::nanoseconds{std::chrono::seconds{1}}.count()));
    return result;
}

typedef std::vector<point> hand_points;

/**
 * Struct which defines a representation of a rectangle
 */
struct rect {
    double x_center = 0.;     //!< x-coordinate of the rectangle's center
    double y_center = 0.;     //!< y-coordinate of the rectangle's center
    double width = 0.;        //!< width of the rectangle (parallel to x-axis when rotation angle is 0)
    double height = 0.;       //!< height of the rectangle (parallel to y-axis when rotation angle is 0)

    double rotation = 0.;     //!< rotation angle of the rectabgle in radians
    bool normalized = false;  //!< if the coordinates are normalized [0..1] (true) or pixel values (false)
    bool valid = false;       //!< if the rectangle is valid

    /**
     * Generic constructor which sets all values to 0
     */
    rect() : x_center(0.), y_center(0.), width(0.), height(0.), rotation(0.), normalized(false), valid(false) {}

    /**
     * Copy constructor
     * @param other The rect to copy
     */
    explicit rect(rect* other) {
        if (other != nullptr) {
            x_center = other->x_center;
            y_center = other->y_center;
            width = other->width;
            height = other->height;
            rotation = other->rotation;
            normalized = other->normalized;
            valid = other->valid;
        }
    }

    /**
     * General constructor
     * @param xc the x-coordinate
     * @param yc the y-coordinate
     * @param w the width
     * @param h the height
     * @param r roation angle
     * @param norm whether the coordinates are normalized
     */
    rect(const double xc, const double yc, const double w, const double h, const double r, bool norm = false) :
        x_center(xc), y_center(yc), width(w), height(h), rotation(r), normalized(norm), valid(true) {}

    /**
     * Set the rect's values after constrution
     * @param xc the x-coordinate
     * @param yc the y-coordinate
     * @param w the width
     * @param h the height
     * @param r roation angle
     * @param norm whether the coordinates are normalized
     */
    void set(const double xc, const double yc, const double w, const double h, const double r, bool norm = false) {
        x_center = xc;
        y_center = yc;
        width = w;
        height = h;

        rotation = r;
        normalized = norm;
        valid = true;
    }
};

enum hand : int {
    LEFT_HAND = 0,
    RIGHT_HAND = 1
};
const std::vector<hand> hand_map{LEFT_HAND, RIGHT_HAND};

struct points_with_validity {
    hand_points points;
    bool valid;
    points_with_validity() : points{std::vector<point>(NUM_LANDMARKS, point())}, valid{false} {}
    points_with_validity(hand_points points_) : points{points_}, valid{true} {}

    point& operator[](size_t i) {
        return points[i];
    }
};
/**
 * Struct containing all the data from a hand detection
 */
struct ht_detection {
    size_t proc_time;             //!< nanoseconds of processing time
    std::map<hand, rect> palms;   //!< left palm detection
    std::map<hand, rect> hands;   //!< left hand detection

    std::map<hand, float> confidence;

    std::map<hand, points_with_validity> points;
    ht_detection() {}
    ht_detection(size_t ptime, rect* lp, rect* rp, rect* lh, rect* rh, float lc, float rc, hand_points *lhp, hand_points *rhp)
        : proc_time{ptime},
        palms{{LEFT_HAND, (lp) ? *lp : rect()}, {RIGHT_HAND, (rp) ? *rp : rect()}},
        hands{{LEFT_HAND, (lh) ? *lh : rect()}, {RIGHT_HAND, (rh) ? *rh : rect()}},
        confidence{{LEFT_HAND, lc}, {RIGHT_HAND, rc}},
        points{{LEFT_HAND, (lhp) ? *lhp : points_with_validity()},
               {RIGHT_HAND, (rhp) ? *rhp : points_with_validity()}} {}
};

struct calculated_point : basic_point {
    double confidence = 0.;

    calculated_point() : basic_point(), confidence(0.) {}
    calculated_point(basic_point bp, double confid) : basic_point{bp}, confidence{confid} {}

    void set(const basic_point& bp) {
        x() = bp.x();
        y() = bp.y();
        z() = bp.z();
    }
};

typedef std::vector<calculated_point> calculated_points;
enum base_unit : int {
    MILLIMETER = 0,
    CENTIMETER = 1,
    METER = 2,
    INCH = 3,
    FOOT = 4,
    UNSET = 5
};

const std::map<base_unit, const std::string> unit_str{{MILLIMETER, "mm"},
                                                      {CENTIMETER, "cm"},
                                                      {METER, "m"},
                                                      {INCH, "in"},
                                                      {FOOT, "ft"},
                                                      {UNSET, "unitless"}
};

struct true_hand_points {
    calculated_points points;
    bool valid;
    base_unit units;

    true_hand_points(base_unit units_ = UNSET) : valid{false}, units{units_} {}
    true_hand_points(const calculated_points points_) :
        points{points_}
        , valid{true} {}

    void set(const calculated_points points_, base_unit units_ = UNSET) {
        points = points_;
        valid = true;
        if (units == UNSET)
            units = units_;
    }
    void clear() {
        points.clear();
        valid = false;
    }
    calculated_point& operator[](size_t i) {
        return points[i];
    }

};

struct ht_frame : cam_base_type {
    std::map<image::image_type, ht_detection> detections;
    std::map<HandTracking::hand, true_hand_points> hand_positions;
    ht_frame() : cam_base_type(time_point(_clock_duration{0}), {}, image::BINOCULAR) {}
    ht_frame(time_point _time, std::map<image::image_type, cv::Mat> _imgs,
             std::map<image::image_type, ht_detection> _detections, std::map<HandTracking::hand, true_hand_points> points)
        : cam_base_type(_time, std::move(_imgs), (_imgs.size() == 2) ? image::BINOCULAR : image::MONOCULAR)
        , detections(std::move(_detections))
        , hand_positions{points} {}

};

struct camera_params {
    float center_x = 0.;        //!> optical center pixel along x-axis
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

    camera_config() : valid{false} {}
    camera_config(std::map<int, camera_params> params, const size_t _w, const size_t _h, const float _fps,
                  float _baseline, base_unit _units) : cam_par(std::move(params))
            , width{_w}
            , height{_h}
            , fps{_fps}
            , baseline{_baseline}
            , units{_units}, valid{true} {}
};
}
