#pragma once
#include <iostream>

#include <eigen3/Eigen/Dense>


namespace ILLIXR {

/**
 * @brief A more complete implementation of the Eigen::Quaternion
 *
 * The base class, Eigen::Quaternion, is missing some arithmetic functionality, like scalar multiplication,
 * this implementation adds this functionality.
 *
 * @tparam Scalar_ The type of data the Quaternion holds (typically double ot float)
 * @tparam Options_ Data alignment
 */
template<typename Scalar_, int Options_ = Eigen::AutoAlign>
class ProperQuaternion : public Eigen::Quaternion<Scalar_, Options_> {
public:
    typedef typename Eigen::Quaternion<Scalar_>::Base Base;

    using Base::operator=;
    /**
     * @brief Copy operator
     * @param other A ProperQuaternion instance
     * @return a copy
     */
    inline ProperQuaternion& operator=(const ProperQuaternion& other) {
        Base::operator=(other);
        return *this;
    }
    using Base::operator*=;

    // constructors
    ProperQuaternion() = default;
    ProperQuaternion(const ProperQuaternion& other) = default;

    /**
     * Copy constructor from the base class
     * @param other An Eigen::Quaternion instance
     */
    [[maybe_unused]] explicit ProperQuaternion(const Eigen::Quaternion<Scalar_, Options_>& other) : Eigen::Quaternion<Scalar_, Options_>(other) {}

    /**
     * Constructor from individual components
     * @param w The w component
     * @param x The x component
     * @param y The y component
     * @param z The z component
     */
    [[maybe_unused]] ProperQuaternion(const Scalar_& w, const Scalar_& x, const Scalar_& y, const Scalar_& z) : Eigen::Quaternion<Scalar_, Options_>(w, x, y, z) {}

    /**
     * Constructor from a scalar and vector
     * @tparam Derived
     * @param w The w component
     * @param vec The x, y, and z components
     */
    template <typename Derived>
    [[maybe_unused]] ProperQuaternion(const Scalar_& w, const Eigen::MatrixBase<Derived>& vec) :
        Eigen::Quaternion<Scalar_, Options_>(w, vec) {}

    /**
     * Constructor from a pointer array (assumes 4 elements)
     * @param data Pointer to the first element
     */
    [[maybe_unused]] explicit ProperQuaternion(const Scalar_* data) : Eigen::Quaternion<Scalar_, Options_>(data) {}

    /**
     * Constructor from a vector. Assumes the vector is x, y, z, w order.
     * @param vec The input vector
     */
    [[maybe_unused]] explicit ProperQuaternion(const Eigen::Vector<Scalar_, 4>& vec) : Eigen::Quaternion<Scalar_, Options_>(vec[3], vec[0], vec[1], vec[2]) {}

    /**
     * Constructor which casts the data type
     * @tparam Derived
     * @param other
     */
    template <typename Derived>
    [[maybe_unused]] explicit ProperQuaternion(const Eigen::QuaternionBase<Derived>& other) :
        Eigen::Quaternion<Scalar_>(other) {}

    /**
     * @brief Scalar multiplication and assignment operator
     * @param a Scalar to multiply the Quaternion by
     * @return Reference to the updated object
     */
    ProperQuaternion& operator*=(const Scalar_& a) {
        this->x() *= a;
        this->y() *= a;
        this->z() *= a;
        this->w() *= a;
        return *this;
    }

    /**
     * @brief Integer multiplication, returning a new instance
     * @param a Integer to multiply the quaternion by
     * @return New instance containing the result
     */
    ProperQuaternion operator*(const int a) const {
        return ProperQuaternion<Scalar_, Options_>(this->w() * a, this->x() * a, this->y() * a, this->z() * a);
    }

    /**
     * @brief Float multiplication, returning a new instance
     * @param a Float to multiply the quaternion by
     * @return New instance containing the result
     */
    ProperQuaternion operator*(const float a) const {
        return ProperQuaternion<Scalar_, Options_>(this->w() * a, this->x() * a, this->y() * a, this->z() * a);
    }

    /**
     * @brief Double multiplication, returning a new instance
     * @param a Double to multiply the quaternion by
     * @return New instance containing the result
     */
    ProperQuaternion operator*(const double a) const {
        return ProperQuaternion<Scalar_, Options_>(this->w() * a, this->x() * a, this->y() * a, this->z() * a);
    }

    /**
     * @brief Multiplication operator with and Eigen::Quaternion
     * @param other The Eigen::Quaternion to multiply by
     * @return The resulting Quaternion
     */
    ProperQuaternion<Scalar_> operator*(const Eigen::Quaternion<Scalar_>& other) const {
        return ProperQuaternion<Scalar_>(Eigen::Quaternion<Scalar_>::operator*(other));
    }

    /**
     * @brief Convert theis object into a 4-element vector, with the order x, y, z, w
     * @return The vector representing this instance.
     */
    Eigen::Vector4<Scalar_> asVector() const {
        return Eigen::Vector4<Scalar_>(this->x(), this->y(), this->z(), this->w());
    }

    /**
     * @brief In place addition operator with another ProperQuaternion
     * @param other A ProperQuaternion to add to this one
     * @return Reference to the updated instance
     */
    ProperQuaternion<Scalar_>& operator+=(const ProperQuaternion<Scalar_> &other) {
        this->w() += other.w();
        this->x() += other.x();
        this->y() += other.y();
        this->z() += other.z();
        return *this;
    }

    /**
     * @brief Addition operator with another ProperQuaternion
     * @param other A ProperQuaternion to add to this one
     * @return The resulting ProperQuaternion as a new instance
     */
    ProperQuaternion<Scalar_> operator+(const ProperQuaternion<Scalar_>& other) const {
        ProperQuaternion<Scalar_> result(*this);
        result += other;
        return result;
    }

    /**
     * @brief Cast a ProperQuaternion from one type to another
     * @tparam T
     * @return The new ProperQuaternion instance of the correct type
     */
    template <typename T>
    ProperQuaternion<T> cast() {
        return ProperQuaternion<T>(static_cast<T>(this->w()),
                                   static_cast<T>(this->x()),
                                   static_cast<T>(this->y()),
                                   static_cast<T>(this->z()));
    }

    /**
      * @brief Normalize the quaternion, but check w first, as it needs to be positive for our conventions
     */
    inline void normalize() {
        if (this->w() < 0.)
            (*this) *= -1.;

        Eigen::Quaternion<Scalar_, Options_>::normalize();
    }

};

/**
 *
 * @tparam T
 * @param os
 * @param pq
 * @return
 */
template <typename T>
std::ostream& operator<<(std::ostream& os, const ProperQuaternion<T>& pq) {
    os << "X " << pq.x() << std::endl << "Y " << pq.y() << std::endl << "Z " << pq.z() << std::endl << "W " << pq.w();
    return os;
}

[[maybe_unused]] typedef ProperQuaternion<double> ProperQuaterniond;
[[maybe_unused]] typedef ProperQuaternion<float> ProperQuaternionf;

/**
 * @brief Multiplication of scalar and ProperQuaternion
 * @tparam Scalar_
 * @param x The value to multiply the quaternion by
 * @param pq The quaternion to multiply
 * @return The ProperQuaternion containing the result
 */
template <typename Scalar_>
inline ProperQuaternion<Scalar_> operator*(Scalar_ x, const ProperQuaternion<Scalar_>& pq) {
    return ProperQuaternion<Scalar_>(x * pq.w(),
                                     x * pq.x(),
                                     x * pq.y(),
                                     x * pq.z());
}

/**
 * @brief Division of a ProperQuaternion by a scalar
 * @tparam Scalar_
 * @param pq The quaternion to divide
 * @param x The value to divide the Quaternion by
 * @return The ProperQuaternion containing the result
 */
template <typename Scalar_>
inline ProperQuaternion<Scalar_> operator/(const ProperQuaternion<Scalar_>& pq, Scalar_ x) {
    return ProperQuaternion<Scalar_>(pq.w() / x,
                                     pq.x() / x,
                                     pq.y() / x,
                                     pq.z() / x);
}

}