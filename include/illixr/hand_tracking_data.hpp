#pragma once

#include "switchboard.hpp"

#include <map>
#include <sstream>
#include <opencv2/core/mat.hpp>

namespace ILLIXR {
const int NUM_LANDMARKS = 21;
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

struct point {
    double x = 0.;
    double y = 0.;
    double z = 0.;
    bool normalized= false;
    bool valid = false;

    point() : x(0.), y(0.), z(0.), normalized(false), valid(false) {
    }

    explicit point(point* other) {
        if (other != nullptr) {
            x = other->x;
            y = other->y;
            z = other->z;
            normalized = other->normalized;
            valid = other->valid;
        }
    }
    point(const double _x, const double _y, const double _z, bool norm = false, bool _valid = true) :
        x(_x), y(_y), z(_z), normalized(norm), valid(_valid) {}

    void set(const double _x, const double _y, const double _z, bool norm = false) {
        x = _x;
        y = _y;
        z = _z;
        normalized = norm;
        valid = true;
    }

    bool operator==(const point &other) const {
        return (x == other.x && y == other.y && z == other.z && normalized == other.normalized && valid == other.valid);
    }

    point operator+(const double &val) const {
        return {x + val, y + val, z + val, normalized, valid};
    }
    point operator-(const double &val) const {
        return {x - val, y - val, z - val, normalized, valid};
    }
    point operator*(const double &val) const {
        return {x * val, y * val, z * val, normalized, valid};
    }
    point operator/(const double &val) const {
        return {x / val, y / val, z / val, normalized, valid};
    }

    point operator+(const point& other) const {
        if(normalized != other.normalized)
            throw std::runtime_error("Invalid unit comparison");
        return {x + other.x, y + other.y, z + other.z, normalized, valid && other.valid};
    }
    point operator-(const point& other) const {
        if(normalized != other.normalized)
            throw std::runtime_error("Invalid unit comparison");
        return {x - other.x, y - other.y, z - other.z, normalized, valid && other.valid};
    }

    point& operator+=(const double &val) {
        x += val;
        y += val;
        z += val;
        return *this;
    }
    point& operator-=(const double &val) {
        x -= val;
        y -= val;
        z -= val;
        return *this;
    }
    point& operator*=(const double &val) {
        x *= val;
        y *= val;
        z *= val;
        return *this;
    }
    point& operator/=(const double &val) {
        x /= val;
        y /= val;
        z /= val;
        return *this;
    }

    point& operator+=(const point &other) {
        if(normalized != other.normalized)
            throw std::runtime_error("Invalid units comparison");
        x += other.x;
        y += other.y;
        z += other.z;
        valid &= other.valid;
        return *this;
    }
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

inline point abs(const point& value) {
    return {std::abs(value.x), std::abs(value.y), std::abs(value.z), value.normalized, value.valid};
}

typedef point velocity;

inline velocity movement(const point &p1, const point &p2, const duration &step) {
    if(p1.normalized != p2.normalized)
        throw std::runtime_error("Invalid unit comparison");
    velocity result = abs(p1) + abs(p2);
    result /= (static_cast<double>(step.count()) / static_cast<double>(std::chrono::nanoseconds{std::chrono::seconds{1}}.count()));
    return result;
}

typedef std::vector<point> hand_points;

struct rect {
    double x_center = 0.;
    double y_center = 0.;
    double width = 0.;
    double height = 0.;

    double rotation = 0.;
    bool normalized = false;
    bool valid = false;

    rect() : x_center(0.), y_center(0.), width(0.), height(0.), rotation(0.), normalized(false), valid(false) {}

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

    rect(const double xc, const double yc, const double w, const double h, const double r, bool norm = false) :
        x_center(xc), y_center(yc), width(w), height(h), rotation(r), normalized(norm), valid(true) {}

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

struct ht_frame : switchboard::event {
    time_point time;
    cv::Mat img;
    rect left_palm;
    rect right_palm;
    rect left_hand;
    rect right_hand;

    float left_confidence;
    float right_confidence;

    hand_points left_hand_points;
    hand_points right_hand_points;

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