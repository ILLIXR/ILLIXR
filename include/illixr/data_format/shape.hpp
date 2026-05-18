#pragma once

#include "normalize.hpp"

#include <cstdint>

namespace ILLIXR {
namespace data_format::shapes {
    /**
     * Struct which defines a representation of a rectangle
     */
    struct [[maybe_unused]] rect {
        double x_center; //!< x-coordinate of the rectangle's center
        double y_center; //!< y-coordinate of the rectangle's center
        double width;    //!< width of the rectangle (parallel to x-axis when rotation angle is 0)
        double height;   //!< height of the rectangle (parallel to y-axis when rotation angle is 0)
        double rotation; //!< rotation angle of the rectangle in radians
        bool   valid;    //!< if the rectangle is valid
        bool   normalized;

        /**
         * Generic constructor which sets all values to 0
         */
        rect()
            : x_center{0.}
            , y_center{0.}
            , width{0.}
            , height{0.}
            , rotation{0.}
            , valid{false}
            , normalized{false} { }

        /**
         * Copy constructor
         * @param other The rect to copy
         */
        explicit rect(rect* other) {
            if (other != nullptr) {
                x_center   = other->x_center;
                y_center   = other->y_center;
                width      = other->width;
                height     = other->height;
                rotation   = other->rotation;
                valid      = other->valid;
                normalized = other->normalized;
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
        rect(const double xc, const double yc, const double w, const double h, const double r, bool normal = false)
            : x_center{xc}
            , y_center{yc}
            , width{w}
            , height{h}
            , rotation{r}
            , valid{true}
            , normalized{normal} { }

        /**
         * Set the rect's values after construction
         * @param xc the x-coordinate
         * @param yc the y-coordinate
         * @param w the width
         * @param h the height
         * @param r rotation angle
         */
        void set(const double xc, const double yc, const double w, const double h, const double r, bool normal = false) {
            x_center   = xc;
            y_center   = yc;
            width      = w;
            height     = h;
            rotation   = r;
            valid      = true;
            normalized = normal;
        }

        /**
         * Flip the y-coordinate about the center axis
         * @param im_height
         */
        void flip_y(const uint32_t im_height = 0) {
            if (normalized) {
                y_center = 1.0 - y_center;
                return;
            }
            if (im_height == 0)
                throw std::runtime_error("Cannot flip rect if no image height is given.");
            y_center = (float) im_height - y_center;
        }
    };
} // namespace data_format::shapes

template<>
inline void normalize<data_format::shapes::rect>(data_format::shapes::rect& obj, const float width, const float height,
                                                 const float depth) {
    (void) depth;
    if (obj.normalized) {
        return;
    }
    obj.x_center /= width;
    obj.y_center /= height;
    obj.width /= width;
    obj.height /= height;
    obj.normalized = true;
}

template<>
inline void denormalize<data_format::shapes::rect>(data_format::shapes::rect& obj, const float width, const float height,
                                                   const float depth) {
    (void) depth;
    if (!obj.valid)
        return;
    if (!obj.normalized) {
        return;
    }
    obj.x_center *= width;
    obj.y_center *= height;
    obj.width *= width;
    obj.height *= height;
    obj.normalized = false;
}

} // namespace ILLIXR
