#pragma once

#include "illixr/data_format/pose.hpp"
#include "illixr/data_format/template.hpp"
#include "illixr/data_format/unit.hpp"

#include <eigen3/Eigen/Dense>

namespace ILLIXR::data_format {
//**********************************************************************************
//  Points
//**********************************************************************************

/*
 * struct representing a point in 3-D space. It is essentially an Eigen::Vector with some additional functions
 */
struct [[maybe_unused]] point : Eigen::Vector3f {
    /*
     * Initial point at the origin
     */
    point()
        : Eigen::Vector3f{0., 0., 0.} { }

    /**
     * Initial point based on the given x, y, and z coordinates
     * @param x The x coordinate in arbitrary units
     * @param y The y coordinate in arbitrary units
     * @param z The z coordinate in arbitrary units, default is 0, indicating a point in 2-D space
     */
    point(const float x, const float y, const float z = 0.)
        : Eigen::Vector3f(x, y, z) { }

    /**
     * Set the coordinates based on the given x, y, and z coordinates
     * @param x The x coordinate in arbitrary units
     * @param y The y coordinate in arbitrary units
     * @param z The z coordinate in arbitrary units, default is 0, indicating a point in 2-D space
     */
    void set(const float x_, const float y_, const float z_ = 0.) {
        x() = x_;
        y() = y_;
        z() = z_;
    }

    /**
     * Assignment operator
     * @param other The point to copy
     * @return A reference to the updated point
     */
    point& operator=(const Eigen::Vector3f& other) {
        x() = other.x();
        y() = other.y();
        z() = other.z();
        return *this;
    }

    /**
     * Assignment operator when dealing with multiplication products
     * @tparam T the type of the left hand side expression
     * @tparam U the type of the right hand side expression
     * @tparam Option
     * @param pr the multiplication product
     * @return a reference to the updated point
     */
    template<typename T, typename U, int Option>
    point& operator=(const Eigen::Product<T, U, Option>& pr) {
        x() = pr.x();
        y() = pr.y();
        z() = pr.z();
        return *this;
    }

    /**
     * Addition operator for adding two points
     * @param other the point to add to this one
     * @return reference to the updated point
     */
    point& operator+=(const Eigen::Vector3f& other) {
        x() += other.x();
        y() += other.y();
        z() += other.z();
        return *this;
    }

    /**
     * Subtraction operator for adding two points
     * @param other the point to subtract from this one
     * @return reference to the updated point
     */
    point& operator-=(const Eigen::Vector3f& other) {
        x() -= other.x();
        y() -= other.y();
        z() -= other.z();
        return *this;
    }
};

/*
 * struct representing a point in 3D space with a flag indicating whether the data are valid/reliable
 */
struct [[maybe_unused]] point_with_validity : point {
    bool  valid      = false; //!< indicates whether the point contains valid data
    float confidence = 0.;    //!< confidence level of the point's value (0. - 1., with 1. indicating 100% confidence)

    /**
     * Default constructor, places point at the origin
     */
    point_with_validity()
        : point()
        , valid{false} { }

    /**
     * Initial point based on the given x, y, and z coordinates
     * @param x The x coordinate in arbitrary units
     * @param y The y coordinate in arbitrary units
     * @param z The z coordinate in arbitrary units
     * @param valid_ Whether the point contains valid data, default is true
     * @param confidence_ Confidence level of the point's value, default is 0.
     */
    point_with_validity(const float x, const float y, const float z, bool valid_ = true, const float confidence_ = 0.)
        : point{x, y, z}
        , valid{valid_}
        , confidence{confidence_} { }

    /**
     * Construct a point_with_validity from another point
     * @param pnt The point to construct it from
     * @param valid_ Whether the point contains valid data, default is true
     * @param confidence_ Confidence level of the point's value, default is 0.
     */
    point_with_validity(const point& pnt, bool valid_ = true, const float confidence_ = 0.)
        : point{pnt}
        , valid{valid_}
        , confidence{confidence_} { }
};

/*
 * struct representing a point in 3D space with a flag indicating whether the data are valid/reliable and the units the point is
 * in
 */
struct [[maybe_unused]] point_with_units : point_with_validity {
    units::measurement_unit unit; //!< The units this point is in

    /**
     * Default constructor, places point at the origin
     */
    explicit point_with_units(units::measurement_unit unit_ = units::UNSET)
        : point_with_validity()
        , unit{unit_} { }

    /**
     * Initial point based on the given x, y, and z coordinates
     * @param x The x coordinate in arbitrary units
     * @param y The y coordinate in arbitrary units
     * @param z The z coordinate in arbitrary units
     * @param unit_ The units of the point, default is UNSET
     * @param valid_ Whether the point contains valid data, default is true
     * @param confidence_ Confidence level of the point's value, default is 0.
     */
    point_with_units(const float x, const float y, const float z, units::measurement_unit unit_ = units::UNSET,
                     bool valid_ = true, const float confidence_ = 0.)
        : point_with_validity{x, y, z, valid_, confidence_}
        , unit{unit_} { }

    /**
     * Construct a point_with_units from another point
     * @param pnt The point to construct it from
     * @param unit_ The units of the point, default is UNSET
     * @param valid_ Whether the point contains valid data, default is true
     * @param confidence_ Confidence level of the point's value, default is 0.
     */
    point_with_units(const point& pnt, units::measurement_unit unit_ = units::UNSET, bool valid_ = true,
                     const float confidence_ = 0.)
        : point_with_validity{pnt, valid_, confidence_}
        , unit{unit_} { }

    /**
     * Construct a point_with_units from a point_with_validity
     * @param pnt The point to construct it from
     * @param unit_ The units of the point, default is UNSET
     */
    explicit point_with_units(const point_with_validity& pnt, units::measurement_unit unit_ = units::UNSET)
        : point_with_validity{pnt}
        , unit{unit_} { }

    /**
     * Addition operator between two points_with_units, units are not checked, but the validity flag is an AND of the two flags
     * @param pnt The point to add to this one
     * @return The new point
     */
    point_with_units operator+(const point_with_units& pnt) const {
        point_with_units p_out;
        p_out.x()   = x() + pnt.x();
        p_out.y()   = y() + pnt.y();
        p_out.z()   = z() + pnt.z();
        p_out.unit  = unit;
        p_out.valid = valid && pnt.valid;
        return p_out;
    }

    /**
     * Subtraction operator between two points_with_units, units are not checked, but the validity flag is an AND of the two
     * flags
     * @param pnt The point to subtract from this one
     * @return The new point
     */
    point_with_units operator-(const point_with_units& pnt) const {
        point_with_units p_out;
        p_out.x()   = x() - pnt.x();
        p_out.y()   = y() - pnt.y();
        p_out.z()   = z() - pnt.z();
        p_out.unit  = unit;
        p_out.valid = valid && pnt.valid;
        return p_out;
    }

    /**
     * Addition operator between a points_with_units and Eigen::Vector
     * @param pnt The point to add to this one
     * @return The new point
     */
    point_with_units operator+(const Eigen::Vector3f& pnt) const {
        point_with_units p_out;
        p_out.x()   = x() + pnt.x();
        p_out.y()   = y() + pnt.y();
        p_out.z()   = z() + pnt.z();
        p_out.unit  = unit;
        p_out.valid = valid;
        return p_out;
    }

    /**
     * Subtraction operator between a points_with_units and Eigen::Vector
     * @param pnt The point to subtract from this one
     * @return The new point
     */
    point_with_units operator-(const Eigen::Vector3f& pnt) const {
        point_with_units p_out;
        p_out.x()   = x() - pnt.x();
        p_out.y()   = y() - pnt.y();
        p_out.z()   = z() - pnt.z();
        p_out.unit  = unit;
        p_out.valid = valid;
        return p_out;
    }

    /**
     * Multiplication operator to multiply a point_with_units by a constant value
     * @param val The value to multiply by
     * @return The new point
     */
    point_with_units operator*(const float val) const {
        point_with_units p_out;
        p_out.x() *= val;
        p_out.y() *= val;
        p_out.z() *= val;
        p_out.unit  = unit;
        p_out.valid = valid;
        return p_out;
    }

    /**
     * Division operator to divide a point_with_units by a constant value
     * @param val The value to divide by
     * @return The new point
     */
    point_with_units operator/(const float val) const {
        point_with_units p_out;
        p_out.x() /= val;
        p_out.y() /= val;
        p_out.z() /= val;
        p_out.unit  = unit;
        p_out.valid = valid;
        return p_out;
    }

    /**
     * Set the coordinates based on the given x, y, and z coordinates
     * @param x The x coordinate in arbitrary units
     * @param y The y coordinate in arbitrary units
     * @param z The z coordinate in arbitrary units
     * @param unit_ The units of the point
     * @param valid_ Whether the point contains valid data, default is true
     */
    void set(const float x_, const float y_, const float z_, units::measurement_unit unit_, bool valid_ = true) {
        x()   = x_;
        y()   = y_;
        z()   = z_;
        unit  = unit_;
        valid = valid_;
    }

    /**
     * Set the coordinates based on the given Eigen::Vector
     * @param vec The vector to set the point from
     */

    void set(const Eigen::Vector3f& vec) {
        x() = vec.x();
        y() = vec.y();
        z() = vec.z();
    }

    /**
     * Set the coordinates from another point_with_units
     * @param pnt The point to set this one to
     */
    void set(const point_with_units& pnt) {
        x()   = pnt.x();
        y()   = pnt.y();
        z()   = pnt.z();
        unit  = pnt.unit;
        valid = pnt.valid;
    }
};

/*
 * Take the absolute value of the point
 */
inline point abs(const point& pnt) {
    return {std::abs(pnt.x()), std::abs(pnt.y()), std::abs(pnt.z())};
}

/*
 * Take the absolute value of the point
 */
[[maybe_unused]] inline point_with_validity abs(const point_with_validity& pnt) {
    return {abs(point(pnt.x(), pnt.y(), pnt.z())), pnt.valid, pnt.confidence};
}

/**
 * Determine the absolute value of a point (done on each coordinate)
 * @param value The point to take the absolute value of
 * @return A point containing the result
 */
[[maybe_unused]] inline point_with_units abs(const point_with_units& pnt) {
    return {abs(point(pnt.x(), pnt.y(), pnt.z())), pnt.unit, pnt.valid, pnt.confidence};
}

/*
 * struct containing a vector of points, along with their unit, and overall validity/reliability
 */
struct [[maybe_unused]] points_with_units {
    std::vector<point_with_units> points; //!< The points
    units::measurement_unit       unit;   //!< The unit of all points
    bool valid; //!< Indicates the validity of all points, will be true if any point is valid, false indicates no points are
                //!< valid
    bool fixed = false; //!< indicates whther the size of the vector has been set

    /**
     * Construct a points_with_units with an empty list
     * @param unit_ The units for the points, default is UNSET
     */
    explicit points_with_units(units::measurement_unit unit_ = units::UNSET)
        : points{std::vector<point_with_units>()}
        , unit{unit_}
        , valid{false} { }

    /**
     * Construct a points_with_units with a list of points with the given size
     * @param size The number of points to initialize
     * @param unit_ The units for the points, default is UNSET
     */
    explicit points_with_units(const int size, units::measurement_unit unit_ = units::UNSET)
        : points{std::vector<point_with_units>(size, point_with_units(unit_))}
        , unit{unit_}
        , valid{false}
        , fixed{true} { }

    /**
     * Construct a points_with_units from a vector of point_with_units objects, units are determined from the points, as is
     * validity
     * @param points_ The points to move into this object
     */
    explicit points_with_units(std::vector<point_with_units> points_)
        : points{std::move(points_)} {
        if (!points.empty())
            unit = points[0].unit;
        valid = true;
        for (const auto& pnt : points)
            valid |= pnt.valid; // it is still valid as long as one point is valid
    }

    /**
     * Copy constructor
     * @param points_
     */
    points_with_units(const points_with_units& points_)
        : points_with_units(points_.points) { }

    /**
     * Copy operator
     * @param other The object to copy into this one
     * @return A reference to the updated points_with_units
     */
    points_with_units& operator=(const points_with_units& other) {
        if (this == &other)
            return *this;
        this->points.resize(other.points.size());
        for (size_t i = 0; i < other.points.size(); i++)
            this->points[i] = other.points[i];
        this->unit  = other.unit;
        this->valid = other.valid;
        this->fixed = other.fixed;
        return *this;
    }

    /**
     * Construct a points_with_units from a vector of point_with_validity objects, validity is determined from the points
     * @param points_ The points to move into this object
     * @param unit_ The units for the points, default is UNSET
     */
    explicit points_with_units(std::vector<point_with_validity>& points_, units::measurement_unit unit_ = units::UNSET)
        : unit{unit_} {
        points.resize(points_.size());
        valid = true;
        for (size_t i = 0; i < points_.size(); i++) {
            points[i] = point_with_units(points_[i], unit_);
            valid |= points_[i].valid; // it is still valid as long as one point is valid
        }
    }

    /**
     * Construct a points_with_units from a vector of point objects
     * @param points_ The points to move into this object
     * @param unit_ The units for the points, default is UNSET
     * @param valid_ The validity of all points, default is true
     */
    explicit points_with_units(std::vector<point>& points_, units::measurement_unit unit_ = units::UNSET, bool valid_ = true)
        : unit{unit_}
        , valid{valid_} {
        points.resize(points_.size());
        for (size_t i = 0; i < points_.size(); i++)
            points[i] = point_with_units(points_[i], unit_, valid_);
    }

    /**
     * Indexing operator to get a specific point_with_units based on index
     * @param idx The index of the requested point_with_units
     * @return A reference to requested point_with_units
     */
    point_with_units& operator[](const size_t idx) {
        if (fixed)
            return points.at(idx);
        return points[idx];
    }

    /**
     * Get the point_with_unit at the given index
     * @param idx The index of the requested point_with_units
     * @return A reference to requested point_with_units
     */
    point_with_units& at(const size_t idx) {
        return points.at(idx);
    }

    /**
     * Get a const version of the point_with_unit at the given index
     * @param idx The index of the requested point_with_units
     * @return A const reference to requested point_with_units
     */
    [[nodiscard]] const point_with_units& at(const size_t idx) const {
        return points.at(idx);
    }

    /**
     * Get the size of the internal vector
     * @return The number of points stored
     */
    [[nodiscard]] size_t size() const {
        return points.size();
    }

    /**
     * Multiply all points by a matrix
     * @param ref_frm The matrix to multiply all points by
     */
    [[maybe_unused]] void mult(const Eigen::Matrix3f& ref_frm) {
        for (point& pnt : points)
            pnt = ref_frm * pnt;
    }

    /**
     * Transform (move) all points by the given pose
     * @param pose The pose to apply to the points
     */
    void transform(const pose_data& pose) {
        for (point& pnt : points)
            pnt = (Eigen::Vector3f) ((pose.orientation * pnt) + pose.position);
    }

    /**
     * Enforce point bounds to be valid for an image (e.g. no negative values
     * @param x_lim upper limit of x values
     * @param y_lim upper limit of y values
     * @param z_lim upper limit of z values
     */
    [[maybe_unused]] void enforce_bounds(const float x_lim = -1., const float y_lim = -1, const float z_lim = -1) {
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

    /**
     * Update the struct validity flag based on the point's flags
     */
    [[maybe_unused]] void check_validity() {
        valid = false;
        for (point_with_validity& pnt : points) {
            if (pnt.valid) {
                valid = true;
                return;
            }
        }
    }
};

/**
 * Normalize a set of points based on the given bounds (all values will be 0. - 1.)
 * @param obj The points to be normalized
 * @param width The x bound
 * @param height The y bound
 * @param depth The z bound
 */
template<>
[[maybe_unused]] inline void normalize<points_with_units>(points_with_units& obj, const float width, const float height,
                                                          const float depth) {
    if (obj.unit == units::PERCENT) {
        std::cout << "Points are already normalized";
        return;
    }
    for (auto& pnt : obj.points)
        ::ILLIXR::data_format::normalize(pnt, width, height, depth);
    obj.unit = units::PERCENT;
}

/**
 * De-normalize a set of points to coordinates within the given bounds
 * @param obj the points to de-normalize
 * @param width the x bound
 * @param height the y bound
 * @param depth the z bound
 * @param unit_ the units for the points
 */
template<>
[[maybe_unused]] inline void denormalize<points_with_units>(points_with_units& obj, const float width, const float height,
                                                            const float depth, units::measurement_unit unit_) {
    for (auto& pnt : obj.points)
        ::ILLIXR::data_format::denormalize(pnt, width, height, depth, unit_);
    obj.unit = unit_;
}

#ifdef ENABLE_OXR
/*
 * This struct is utilized when working with OpenXR. The internal variables are in a basic form since OpenXR uses
 * C, rather than C++ (e.g. points do not inherit from Eigen::Vector)
 */
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

    [[maybe_unused]] explicit raw_point(const point_with_units& pnt)
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

    [[maybe_unused]] void mult(const Eigen::Matrix3f& ref_frm) {
        Eigen::Vector3f vec{x, y, z};
        vec = ref_frm * vec;
        x   = vec.x();
        y   = vec.y();
        z   = vec.z();
    }

    void transform(const pose_data& pose) {
        Eigen::Vector3f vec{x, y, z};
        vec = (Eigen::Vector3f) ((pose.orientation * vec) + pose.position);
        x   = vec.x();
        y   = vec.y();
        z   = vec.z();
    }

    [[maybe_unused]] void de_transform(const pose_data& pose) {
        Eigen::Vector3f vec{x, y, z};
        vec -= pose.position;
        vec = (Eigen::Vector3f) ((pose.orientation.inverse() * vec));
        x   = vec.x();
        y   = vec.y();
        z   = vec.z();
    }
};
#endif
} // namespace ILLIXR::data_format
