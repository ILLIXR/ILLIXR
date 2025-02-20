#pragma once

#include "illixr/data_format/template.hpp"
#include "illixr/data_format/unit.hpp"

#include <spdlog/spdlog.h>

namespace ILLIXR::data_format {
/**
 * Struct which defines a representation of a rectangle
 */
struct [[maybe_unused]] rect {
    double x_center; //!< x-coordinate of the rectangle's center
    double y_center; //!< y-coordinate of the rectangle's center
    double width;    //!< width of the rectangle (parallel to x-axis when rotation angle is 0)
    double height;   //!< height of the rectangle (parallel to y-axis when rotation angle is 0)

    double                  rotation; //!< rotation angle of the rectangle in radians
    units::measurement_unit unit;
    bool                    valid; //!< if the rectangle is valid

    /**
     * Generic constructor which sets all values to 0
     */
    rect()
        : x_center{0.}
        , y_center{0.}
        , width{0.}
        , height{0.}
        , rotation{0.}
        , unit{units::UNSET}
        , valid{false} { }

    /**
     * Copy constructor
     * @param other The rect to copy
     */
    explicit rect(rect* other) {
        if (other != nullptr) {
            x_center = other->x_center;
            y_center = other->y_center;
            width    = other->width;
            height   = other->height;
            rotation = other->rotation;
            unit     = other->unit;
            valid    = other->valid;
        }
    }

    /**
     * General constructor
     * @param xc the x-coordinate
     * @param yc the y-coordinate
     * @param w the width
     * @param h the height
     * @param r rotation angle
     */
    rect(const double xc, const double yc, const double w, const double h, const double r,
         units::measurement_unit unit_ = units::UNSET)
        : x_center{xc}
        , y_center{yc}
        , width{w}
        , height{h}
        , rotation{r}
        , unit{unit_}
        , valid{true} { }

    /**
     * Set the rect's values after construction
     * @param xc the x-coordinate
     * @param yc the y-coordinate
     * @param w the width
     * @param h the height
     * @param r rotation angle
     */
    void set(const double xc, const double yc, const double w, const double h, const double r,
             units::measurement_unit unit_ = units::UNSET) {
        x_center = xc;
        y_center = yc;
        width    = w;
        height   = h;

        rotation = r;
        unit     = unit_;
        valid    = true;
    }

    void flip_y(const uint im_height = 0) {
        if (unit == units::PERCENT) {
            y_center = 1.0 - y_center;
            return;
        }
        if (im_height == 0)
            throw std::runtime_error("Cannot rectify rect with non percent units if no image height is given.");
        y_center = (float) im_height - y_center;
    }
};

template<>
inline void normalize<rect>(rect& obj, const float width, const float height, const float depth) {
    (void) depth;
    if (obj.unit == units::PERCENT) {
        spdlog::get("illixr")->info("[normalize] rect already normalized");
        return;
    }
    obj.x_center /= width;
    obj.y_center /= height;
    obj.width /= width;
    obj.height /= height;
    obj.unit = units::PERCENT;
}

template<>
inline void denormalize<rect>(rect& obj, const float width, const float height, const float depth,
                              units::measurement_unit unit) {
    (void) depth;
    if (!obj.valid)
        return;
    if (obj.unit != units::PERCENT) {
        spdlog::get("illixr")->info("[denormalize] rect already denormalized");
        return;
    }
    if (unit == units::PERCENT)
        throw std::runtime_error("Cannot denormalize to PERCENT");
    obj.x_center *= width;
    obj.y_center *= height;
    obj.width *= width;
    obj.height *= height;
    obj.unit = unit;
}

} // namespace ILLIXR::data_format
