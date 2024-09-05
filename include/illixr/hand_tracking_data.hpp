#pragma once

#include "switchboard.hpp"

#include <map>
#include <sstream>
#include <opencv2/core/mat.hpp>

namespace ILLIXR {
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
const std::map<int, std::string> point_map {{WRIST, "Wrist"},
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

/**
 * Struct for containing the 3D coordinates of a point
 */
struct point {
    double x = 0.;           //!< x-coordiante
    double y = 0.;           //!< y-coordinate
    double z = 0.;           //!< z_coordinate
    bool normalized= false;  //!< if the coordinates are normalized [0..1] (true) or pixel values (false)
    bool valid = false;      //!< if the point is valid

    /**
     * Generic constructor which sets all valies to 0.
     */
    point() : x(0.), y(0.), z(0.), normalized(false), valid(false) {
    }

    /**
     * Copy constructor
     * @param other the point to initialize this one with
     */
    explicit point(point* other) {
        if (other != nullptr) {
            x = other->x;
            y = other->y;
            z = other->z;
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
    point(const double _x, const double _y, const double _z, bool norm = false, bool _valid = true) :
        x(_x), y(_y), z(_z), normalized(norm), valid(_valid) {}

    /**
     * Set the values of an already existing point
     * @param _x the x coordinate
     * @param _y the y coordinate
     * @param _z the z coordinate
     * @param norm whether the corrdinates are normalized, or raw pixel values
     */
    void set(const double _x, const double _y, const double _z, bool norm = false) {
        x = _x;
        y = _y;
        z = _z;
        normalized = norm;
        valid = true;
    }

    /**
     * Equality comparison
     * @param other The point to compare this one to
     * @return true if they are the same
     */
    bool operator==(const point &other) const {
        return (x == other.x && y == other.y && z == other.z && normalized == other.normalized && valid == other.valid);
    }

    /**
     * Multiplication operator for scaling the internal values by a constant
     * @param val The value to scale the point by
     * @return
     */
    point operator*(const double &val) const {
        return {x * val, y * val, z * val, normalized, valid};
    }
    /**
     * Division operator for scaling the internal values by a constant
     * @param val The value to scale the point by
     * @return
     */
    point operator/(const double &val) const {
        return {x / val, y / val, z / val, normalized, valid};
    }

    /**
     * Operator to add two points together
     * @param other The point to add to this one
     * @return a new point containing the addition operation results
     */
    point operator+(const point& other) const {
        if(normalized != other.normalized)
            throw std::runtime_error("Invalid unit comparison");
        return {x + other.x, y + other.y, z + other.z, normalized, valid && other.valid};
    }
    /**
     * Operator to subtract another point from this one
     * @param other The point to subtract from this one
     * @return a new point containing the subtraction operation results
     */
    point operator-(const point& other) const {
        if(normalized != other.normalized)
            throw std::runtime_error("Invalid unit comparison");
        return {x - other.x, y - other.y, z - other.z, normalized, valid && other.valid};
    }

    /**
     * In place multiplication operator for scaling the internal values by a constant
     * @param val The value to scale the point by
     * @return
     */
    point& operator*=(const double &val) {
        x *= val;
        y *= val;
        z *= val;
        return *this;
    }
    /**
     * In place division operator for scaling the internal values by a constant
     * @param val The value to scale the point by
     * @return
     */
    point& operator/=(const double &val) {
        x /= val;
        y /= val;
        z /= val;
        return *this;
    }

    /**
     * In place operator to add two points together
     * @param other The point to add to this one
     * @return a new point containing the addition operation results
     */
    point& operator+=(const point &other) {
        if(normalized != other.normalized)
            throw std::runtime_error("Invalid units comparison");
        x += other.x;
        y += other.y;
        z += other.z;
        valid &= other.valid;
        return *this;
    }
    /**
     * In place operator to add two points together
     * @param other The point to add to this one
     * @return a new point containing the addition operation results
     */
    point& operator-=(const point &other) {
        if(normalized != other.normalized)
            throw std::runtime_error("Invalid units comparison");
        x -= other.x;
        y -= other.y;
        z -= other.z;
        valid &= other.valid;
        return *this;
    }
    [[nodiscard]]std::string to_string() const {
        std::ostringstream buffer;
        buffer.setf(std::ios::fixed);
        buffer.precision(2);
        buffer << x << ", " << y << ", " << z;
        return buffer.str();
    }
};

/**
 * Determine the absolute value of a point (done on each coordinate)
 * @param value The point to take the absolute value of
 * @return A point containing the result
 */
inline point abs(const point& value) {
    return {std::abs(value.x), std::abs(value.y), std::abs(value.z), value.normalized, value.valid};
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

/**
 * Struct containing all the data from a hand detection
 */
struct ht_frame : switchboard::event {
    time_point time;    //!< timestamp
    cv::Mat img;        //!< image containing the results RGBA format
    rect left_palm;     //!< left palm detection
    rect right_palm;    //!< right palm detection
    rect left_hand;     //!< left hand detection
    rect right_hand;    //!< right hand detection

    float left_confidence;   //!< left hand detection confidence
    float right_confidence;  //!< right hand detection confidence

    hand_points left_hand_points;  //!< left hand landmark points
    hand_points right_hand_points; //!< right hand landmark points

    ht_frame(time_point _time, cv::Mat* _img, rect* lp, rect* rp, rect* lh, rect* rh, float lc, float rc, hand_points *lhp, hand_points *rhp)
        : time(_time), left_confidence(lc), right_confidence(rc) {
        if (_img) {
            _img->copyTo(img);
        } else {
            img = cv::Mat();
        }
        //delete _img;
        left_palm  = (lp) ? *lp : rect();
        //delete lp;
        right_palm = (rp) ? *rp : rect();
        //delete rp;
        left_hand  = (lh) ? *lh : rect();
        //delete lh;
        right_hand = (rh) ? *rh : rect();
        //delete rh;
        if (lhp) {
            left_hand_points = *lhp;
            //delete lhp;
        } else {
            left_hand_points.assign(NUM_LANDMARKS, point());
        }
        if (rhp) {
            right_hand_points = *rhp;
            //delete rhp;
        } else {
            right_hand_points.assign(NUM_LANDMARKS, point());
        }
    }
};

}
