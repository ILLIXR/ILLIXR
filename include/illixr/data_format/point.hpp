#pragma once

#include "illixr/data_format/pose.hpp"
#include "illixr/data_format/template.hpp"
#include "illixr/data_format/unit.hpp"

#include <eigen3/Eigen/Dense>

namespace ILLIXR::data_format {
//**********************************************************************************
//  Points
//**********************************************************************************

struct [[maybe_unused]] point : Eigen::Vector3f {
    point()
        : Eigen::Vector3f{0., 0., 0.} { }

    point(const float x, const float y, const float z = 0.)
        : Eigen::Vector3f(x, y, z) { }

    void set(const float x_, const float y_, const float z_ = 0.) {
        x() = x_;
        y() = y_;
        z() = z_;
    }

    point& operator=(const Eigen::Vector3f& other) {
        x() = other.x();
        y() = other.y();
        z() = other.z();
        return *this;
    }

    template<typename T, typename U, int Option>
    point& operator=(const Eigen::Product<T, U, Option>& pr) {
        x() = pr.x();
        y() = pr.y();
        z() = pr.z();
        return *this;
    }

    point& operator+=(const Eigen::Vector3f& other) {
        x() += other.x();
        y() += other.y();
        z() += other.z();
        return *this;
    }

    point& operator-=(const Eigen::Vector3f& other) {
        x() -= other.x();
        y() -= other.y();
        z() -= other.z();
        return *this;
    }
};

struct [[maybe_unused]] point_with_validity : point {
    bool  valid      = false;
    float confidence = 0.;

    point_with_validity()
        : point()
        , valid{false} { }

    point_with_validity(const float x, const float y, const float z, bool valid_ = true, const float confidence_ = 0.)
        : point{x, y, z}
        , valid{valid_}
        , confidence{confidence_} { }

    point_with_validity(const point& pnt, bool valid_ = true, const float confidence_ = 0.)
        : point{pnt}
        , valid{valid_}
        , confidence{confidence_} { }
};

struct [[maybe_unused]] point_with_units : point_with_validity {
    units::measurement_unit unit;

    explicit point_with_units(units::measurement_unit unit_ = units::UNSET)
        : point_with_validity()
        , unit{unit_} { }

    point_with_units(const float x, const float y, const float z, units::measurement_unit unit_ = units::UNSET,
                     bool valid_ = true, const float confidence_ = 0.)
        : point_with_validity{x, y, z, valid_, confidence_}
        , unit{unit_} { }

    point_with_units(const point& pnt, units::measurement_unit unit_ = units::UNSET, bool valid_ = true,
                     const float confidence_ = 0.)
        : point_with_validity{pnt, valid_, confidence_}
        , unit{unit_} { }

    explicit point_with_units(const point_with_validity& pnt, units::measurement_unit unit_ = units::UNSET)
        : point_with_validity{pnt}
        , unit{unit_} { }

    point_with_units operator+(const point_with_units& pnt) const {
        point_with_units p_out;
        p_out.x()   = x() + pnt.x();
        p_out.y()   = y() + pnt.y();
        p_out.z()   = z() + pnt.z();
        p_out.unit  = unit;
        p_out.valid = valid && pnt.valid;
        return p_out;
    }

    point_with_units operator-(const point_with_units& pnt) const {
        point_with_units p_out;
        p_out.x()   = x() - pnt.x();
        p_out.y()   = y() - pnt.y();
        p_out.z()   = z() - pnt.z();
        p_out.unit  = unit;
        p_out.valid = valid && pnt.valid;
        return p_out;
    }

    point_with_units operator+(const Eigen::Vector3f& pnt) const {
        point_with_units p_out;
        p_out.x()   = x() + pnt.x();
        p_out.y()   = y() + pnt.y();
        p_out.z()   = z() + pnt.z();
        p_out.unit  = unit;
        p_out.valid = valid;
        return p_out;
    }

    point_with_units operator-(const Eigen::Vector3f& pnt) const {
        point_with_units p_out;
        p_out.x()   = x() - pnt.x();
        p_out.y()   = y() - pnt.y();
        p_out.z()   = z() - pnt.z();
        p_out.unit  = unit;
        p_out.valid = valid;
        return p_out;
    }

    point_with_units operator*(const float val) const {
        point_with_units p_out;
        p_out.x() *= val;
        p_out.y() *= val;
        p_out.z() *= val;
        p_out.unit  = unit;
        p_out.valid = valid;
        return p_out;
    }

    point_with_units operator/(const float val) const {
        point_with_units p_out;
        p_out.x() /= val;
        p_out.y() /= val;
        p_out.z() /= val;
        p_out.unit  = unit;
        p_out.valid = valid;
        return p_out;
    }

    void set(const float x_, const float y_, const float z_, units::measurement_unit unit_, bool valid_ = true) {
        x()   = x_;
        y()   = y_;
        z()   = z_;
        unit  = unit_;
        valid = valid_;
    }

    void set(const Eigen::Vector3f& vec) {
        x() = vec.x();
        y() = vec.y();
        z() = vec.z();
    }

    void set(const point_with_units& pnt) {
        x()   = pnt.x();
        y()   = pnt.y();
        z()   = pnt.z();
        unit  = pnt.unit;
        valid = pnt.valid;
    }
};

inline point abs(const point& pnt) {
    return {std::abs(pnt.x()), std::abs(pnt.y()), std::abs(pnt.z())};
}

inline point_with_validity abs(const point_with_validity& pnt) {
    return {abs(point(pnt.x(), pnt.y(), pnt.z())), pnt.valid, pnt.confidence};
}

/**
 * Determine the absolute value of a point (done on each coordinate)
 * @param value The point to take the absolute value of
 * @return A point containing the result
 */
inline point_with_units abs(const point_with_units& pnt) {
    return {abs(point(pnt.x(), pnt.y(), pnt.z())), pnt.unit, pnt.valid, pnt.confidence};
}

struct [[maybe_unused]] points_with_units {
    std::vector<point_with_units> points;
    units::measurement_unit       unit;
    bool                          valid;
    bool                          fixed = false;

    explicit points_with_units(units::measurement_unit unit_ = units::UNSET)
        : points{std::vector<point_with_units>()}
        , unit{unit_}
        , valid{false} { }

    explicit points_with_units(const int size, units::measurement_unit unit_ = units::UNSET)
        : points{std::vector<point_with_units>(size, point_with_units(unit_))}
        , unit{unit_}
        , valid{false}
        , fixed{true} { }

    explicit points_with_units(std::vector<point_with_units> points_)
        : points{std::move(points_)} {
        if (!points.empty())
            unit = points[0].unit;
        valid = true;
        for (const auto& pnt : points)
            valid |= pnt.valid; // it is still valid as long as one point is valid
    }

    points_with_units(const points_with_units& points_)
        : points_with_units(points_.points) { }

    explicit points_with_units(std::vector<point_with_validity>& points_, units::measurement_unit unit_ = units::UNSET)
        : unit{unit_} {
        points.resize(points_.size());
        valid = true;
        for (size_t i = 0; i < points_.size(); i++) {
            points[i] = point_with_units(points_[i], unit_);
            valid |= points_[i].valid; // it is still valid as long as one point is valid
        }
    }

    explicit points_with_units(std::vector<point>& points_, units::measurement_unit unit_ = units::UNSET, bool valid_ = true)
        : unit{unit_}
        , valid{valid_} {
        points.resize(points_.size());
        for (size_t i = 0; i < points_.size(); i++)
            points[i] = point_with_units(points_[i], unit_, valid_);
    }

    point_with_units& operator[](const size_t idx) {
        if (fixed)
            return points.at(idx);
        return points[idx];
    }

    point_with_units& at(const size_t idx) {
        return points.at(idx);
    }

    [[nodiscard]] const point_with_units& at(const size_t idx) const {
        return points.at(idx);
    }

    [[nodiscard]] size_t size() const {
        return points.size();
    }

    void mult(const Eigen::Matrix3f& ref_frm) {
        for (point& pnt : points)
            pnt = ref_frm * pnt;
    }

    void transform(const pose_data& pose) {
        for (point& pnt : points)
            pnt = (Eigen::Vector3f)((pose.orientation * pnt) + pose.position);
    }

    void enforce_bounds(const float x_lim = -1., const float y_lim = -1, const float z_lim = -1) {
        for (auto& pnt : points) {
            if (!pnt.valid)
                continue;
            if (x_lim > 0.) {
                if (pnt.x() < 0. || pnt.x() >= x_lim) {
                    pnt.valid = false;
                    continue;
                }
            }
            if (y_lim > 0.) {
                if (pnt.y() < 0. || pnt.y() >= y_lim) {
                    pnt.valid = false;
                    continue;
                }
            }
            if (z_lim > 0.) {
                if (pnt.z() < 0. || pnt.z() >= z_lim) {
                    pnt.valid = false;
                    continue;
                }
            }
        }
    }

    void check_validity() {
        valid = false;
        for (point_with_validity& pnt : points) {
            if (pnt.valid) {
                valid = true;
                return;
            }
        }
    }
};

template<>
inline void normalize<points_with_units>(points_with_units& obj, const float width, const float height, const float depth) {
    if (obj.unit == units::PERCENT) {
        std::cout << "Points are already normalized";
        return;
    }
    for (auto& pnt : obj.points)
        ::ILLIXR::data_format::normalize(pnt, width, height, depth);
    obj.unit = units::PERCENT;
}

template<>
inline void denormalize<points_with_units>(points_with_units& obj, const float width, const float height, const float depth,
                                           units::measurement_unit unit_) {
    for (auto& pnt : obj.points)
        ::ILLIXR::data_format::denormalize(pnt, width, height, depth, unit_);
    obj.unit = unit_;
}

struct raw_point {
    float x;
    float y;
    float z;
    bool  valid;

    raw_point()
        : x{0.f}
        , y{0.f}
        , z{0.f}
        , valid{false} { }

    explicit raw_point(const point_with_units& pnt)
        : x{pnt.x()}
        , y{pnt.y()}
        , z{pnt.z()}
        , valid{pnt.valid} { }

    void copy(const point_with_units& pnt) {
        x     = pnt.x();
        y     = pnt.y();
        z     = pnt.z();
        valid = pnt.valid;
    }

    void mult(const Eigen::Matrix3f& ref_frm) {
        Eigen::Vector3f vec{x, y, z};
        vec = ref_frm * vec;
        x   = vec.x();
        y   = vec.y();
        z   = vec.z();
    }

    void transform(const pose_data& pose) {
        Eigen::Vector3f vec{x, y, z};
        vec = (Eigen::Vector3f)((pose.orientation * vec) + pose.position);
        x   = vec.x();
        y   = vec.y();
        z   = vec.z();
    }

    void de_transform(const pose_data& pose) {
        Eigen::Vector3f vec{x, y, z};
        vec -= pose.position;
        vec = (Eigen::Vector3f)((pose.orientation.inverse() * vec));
        x   = vec.x();
        y   = vec.y();
        z   = vec.z();
    }
};

} // namespace ILLIXR::data_format
