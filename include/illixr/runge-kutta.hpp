#pragma once
#include <iostream>
#include <eigen3/Eigen/Dense>
#include "comp.hpp"

namespace ILLIXR {

template<typename Scalar_, int Options_ = Eigen::AutoAlign>
class ProperQuaternion : public Eigen::Quaternion<Scalar_, Options_> {
public:
    typedef typename Eigen::Quaternion<Scalar_>::Base Base;
//    typedef Eigen::QuaternionBase<Eigen::Quaternion<Scalar_, Options_> > Base;
    typedef typename Base::AngleAxisType AngleAxisType;

    using Base::operator=;
    inline ProperQuaternion& operator=(const ProperQuaternion& other) {
        Base::operator=(other);
        return *this;
    }
    using Base::operator*=;

    ProperQuaternion() = default;
    ProperQuaternion(const ProperQuaternion& other) = default;
    ProperQuaternion(const Eigen::Quaternion<Scalar_, Options_>& other) : Eigen::Quaternion<Scalar_, Options_>(other) {}
    ProperQuaternion(const Scalar_& w, const Scalar_& x, const Scalar_& y, const Scalar_& z) : Eigen::Quaternion<Scalar_, Options_>(w, x, y, z) {}
    template <typename Derived>
    ProperQuaternion(const Scalar_& w, const Eigen::MatrixBase<Derived>& vec) :
        Eigen::Quaternion<Scalar_, Options_>(w, vec) {}
    ProperQuaternion(const Scalar_* data) : Eigen::Quaternion<Scalar_, Options_>(data) {}
    ProperQuaternion(const Eigen::Vector<double, 4>& vec) : Eigen::Quaternion<Scalar_, Options_>(vec[0], vec[1], vec[2], vec[3]) {}

    template <typename Derived>
    ProperQuaternion(const Eigen::QuaternionBase<Derived>& other) :
        Eigen::Quaternion<Scalar_>(other) {}

    ProperQuaternion& operator*=(const Scalar_& a) {
        this->x() *= a;
        this->y() *= a;
        this->z() *= a;
        this->w() *= a;
        return *this;
    }

    ProperQuaternion operator*(const int a) const {
        return ProperQuaternion<Scalar_, Options_>(this->w() * a, this->x() * a, this->y() * a, this->z() * a);
    }
    ProperQuaternion operator*(const float a) const {
        return ProperQuaternion<Scalar_, Options_>(this->w() * a, this->x() * a, this->y() * a, this->z() * a);
    }
    ProperQuaternion operator*(const double a) const {
        return ProperQuaternion<Scalar_, Options_>(this->w() * a, this->x() * a, this->y() * a, this->z() * a);
    }

    ProperQuaternion<Scalar_> operator*(const Eigen::Quaternion<Scalar_>& other) const {
        return ProperQuaternion<Scalar_>(Eigen::Quaternion<Scalar_>::operator*(other));
    }
    Eigen::Vector4<Scalar_> asVector() const {
        return Eigen::Vector4<Scalar_>(this->x(), this->y(), this->z(), this->w());
    }

    ProperQuaternion<Scalar_>& operator+=(const ProperQuaternion<Scalar_> &other) {
        this->w() += other.w();
        this->x() += other.x();
        this->y() += other.y();
        this->z() += other.z();
        return *this;
    }
    ProperQuaternion<Scalar_> operator+(const ProperQuaternion<Scalar_>& other) const {
        ProperQuaternion<Scalar_> result(*this);
        result += other;
        return result;
    }
    template <typename T>
    ProperQuaternion<T> cast() {
        return ProperQuaternion<T>(static_cast<T>(this->w()),
                                   static_cast<T>(this->x()),
                                   static_cast<T>(this->y()),
                                   static_cast<T>(this->z()));
    }

};

/*template <typename T>
Eigen::Quaternion<T> operator=(const ProperQuaternion<T>& pq) {
    Eigen::Quaternion<T> out;
    out.w() = pq.w();
    out.x() = pq.x();
    out.y() = pq.y();
    out.z() = pq.z();
    return out;
}*/

template <typename T>
std::ostream& operator<<(std::ostream& os, const ProperQuaternion<T>& pq) {
    os << "X " << pq.x() << std::endl << "Y " << pq.y() << std::endl << "Z " << pq.z() << std::endl << "W " << pq.w();
    return os;
}

typedef ProperQuaternion<double> ProperQuaterniond;
typedef ProperQuaternion<float> ProperQuaternionf;

/**
 * @brief Generates a skew-symmetric matrix from the given 3-element vector
 *
 * Based on equation 6 from http://mars.cs.umn.edu/tr/reports/Trawny05b.pdf
 */
inline Eigen::Matrix3d symmetric_skew(const Eigen::Vector3d& vec, Compare& cc) {
    Eigen::Matrix3d skew_q;
    skew_q << 0, -vec[2], vec[1],
        vec[2], 0, -vec[0],
        -vec[1], vec[0], 0;
    cc.skew.push_back(skew_q);
    return skew_q;
}

/**
 * @brief Generate the Omega matrix from the input 3-element vector
 *
 * Based on equation 48 of http://mars.cs.umn.edu/tr/reports/Trawny05b.pdf
 */
inline Eigen::Matrix4d makeOmega(const Eigen::Vector3d& w, Compare& cc) {
    Eigen::Matrix4d omega = Eigen::Matrix4d::Zero();
    omega.block<3, 3>(0, 0) = -symmetric_skew(w, cc);
    omega.block<1, 3>(3, 0) = -w.transpose();
    omega.block<3, 1>(0, 3) = w;
    cc.omega.push_back(omega);
    return omega;
}

const ProperQuaterniond dq_0(1., 0., 0., 0.);
const Eigen::Vector3d Gravity(0.0, 0.0, 9.81);
/**
 * @brief Normalize the quaternion, but check w first
 */
inline void normalize(ProperQuaternion<double>& quat) {
    if (quat.w() < 0.)
        quat *= -1.;
    quat.normalize();
}

//template <typename Scalar_>
//inline ProperQuaternion<Scalar_>& operator*(Scalar_ x, ProperQuaternion<Scalar_>& pq) {
//    pq *= x;
//    return pq;
//}

template <typename Scalar_>
inline ProperQuaternion<Scalar_> operator*(Scalar_ x, const ProperQuaternion<Scalar_>& pq) {
    return ProperQuaternion<Scalar_>(x * pq.w(),
                                     x * pq.x(),
                                     x * pq.y(),
                                     x * pq.z());
}

template <typename Scalar_>
inline ProperQuaternion<Scalar_> operator/(const ProperQuaternion<Scalar_>& pq, Scalar_ x) {
    return ProperQuaternion<Scalar_>(pq.w() / x,
                                     pq.x() / x,
                                     pq.y() / x,
                                     pq.z() / x);
}

inline
ProperQuaterniond delta_q(const ProperQuaterniond& k_n) {
    ProperQuaterniond dq(dq_0 + 0.5 * k_n);
    dq.normalize();
    return dq;
}

inline
ProperQuaterniond q_dot(const Eigen::Vector3d& av, const ProperQuaterniond& dq, Compare& cc) {
    return ProperQuaterniond(Eigen::Vector4d(0.5 * makeOmega(av, cc) * dq.asVector()));
}

inline
Eigen::Vector3d delta_v(const Eigen::Vector3d& iv, const Eigen::Vector3d& k_n) {
    return Eigen::Vector3d(iv + 0.5 * k_n);
}

/**
 * @brief calculate the v dot value
 * @param dq
 * @param q
 * @param l_acc
 * @return
 */
inline
Eigen::Vector3d v_dot(const ProperQuaterniond& dq, const ProperQuaterniond& q,
          const Eigen::Vector3d& l_acc, Compare& cc) {
    ProperQuaternion temp = dq * q;
    std::cout << "S" << std::endl << std::endl;
    std::cout << dq << std::endl << std::endl;
    std::cout << q << std::endl << std::endl;
    std::cout << temp.asVector() << std::endl;
    std::cout << "E" << std::endl;
    cc.mult.push_back(temp.asVector());
    temp.normalize();
    Eigen::Matrix3d R_Gto0 = temp.toRotationMatrix();
    cc.rot.push_back(R_Gto0);
    return R_Gto0.transpose() * l_acc - Gravity;
}

/**
 * @brief Solve for the Runge-Kutta 4th order approximation
 * @tparam T The data type
 * @param yn The value at the last time step
 * @param k1 Runge-Kutta first order
 * @param k2 Runge-Kutta second order
 * @param k3 Runge-Kutta third order
 * @param k4 Runge-Kutta fourth order
 * @return The approximated value
 */
template<typename T>
inline T solve(const T& yn, const T& k1, const T& k2, const T& k3, const T& k4) {
    return yn + (k1 + 2. * (k2 + k3) + k4) / 6.;
}

struct StatePlus {
    ProperQuaterniond orientation;
    Eigen::Vector3d velocity;
    Eigen::Vector3d position;
};

std::ostream& operator<< (std::ostream& os, const StatePlus& sp) {
    os << "Quat " << sp.orientation << std::endl;
    os << "Vel  " << sp.velocity << std::endl;
    os << "Pos  " << sp.position << std::endl;
    return os;
}

StatePlus predict_mean_rk4(double dt, const StatePlus& sp,
                           const Eigen::Vector3d& ang_vel,
                           const Eigen::Vector3d& linear_acc,
                           const Eigen::Vector3d& ang_vel2,
                           const Eigen::Vector3d& linear_acc2,
                           Compare& cc) {

    Eigen::Vector3d av = ang_vel;
    Eigen::Vector3d la = linear_acc;
    const Eigen::Vector3d delta_av = (ang_vel2 - ang_vel) / dt;
    const Eigen::Vector3d delta_la = (linear_acc2 - linear_acc) / dt;

        cc.delta_w = delta_av;
        cc.delta_a = delta_la;

    // y0 ================
    ProperQuaterniond  q_0 = sp.orientation;  // initial orientation quaternion
    Eigen::Vector3d    p_0 = sp.position;   // initial position vector
    Eigen::Vector3d    v_0 = sp.velocity;   // initial velocity vector

    // solve orientation
    // k1
    ProperQuaterniond q0_dot = q_dot(av, dq_0, cc);
    ProperQuaterniond k1_q = q0_dot * dt;

    cc.q0_dot = q0_dot;
    av += 0.5 * delta_av * dt;

    cc.w_hat1 = av;

    // k2
    ProperQuaterniond dq_1 = delta_q(k1_q);
    ProperQuaterniond q1_dot = q_dot(av, dq_1, cc);
    ProperQuaterniond k2_q = q1_dot * dt;

    // k3
    ProperQuaterniond dq_2 = delta_q(k2_q);
    ProperQuaterniond q2_dot = q_dot(av, dq_2, cc);
    ProperQuaterniond k3_q = q2_dot * dt;

    // k4
    av += 0.5 * delta_av * dt;
    cc.w_hat2 = av;
    ProperQuaterniond dq_3 = delta_q(2. * k3_q);
    ProperQuaterniond q3_dot = q_dot(av, dq_3, cc);
    ProperQuaterniond k4_q = q3_dot * dt;

    // solve velocity
    // k1
    Eigen::Vector3d v0d = v_dot(dq_0, q_0, la, cc);
    Eigen::Vector3d k1_v = v0d * dt;
        cc.v0_dot = v0d;

    // k2
    la += 0.5 * delta_la * dt;
    cc.a_hat1 = la;
    Eigen::Vector3d v_1  = delta_v(v_0, k1_v);
    Eigen::Vector3d k2_v = v_dot(dq_1, q_0, la, cc) * dt;

    // k3
    Eigen::Vector3d v_2 = delta_v(v_0, k2_v);
    Eigen::Vector3d k3_v = v_dot(dq_2, q_0, la, cc) * dt;

    // k4
    la += 0.5 * delta_la * dt;
    cc.a_hat2 = la;
    Eigen::Vector3d v_3 = delta_v(v_0, 2. * k3_v);
    Eigen::Vector3d k4_v = v_dot(dq_3, q_0, la, cc) * dt;


    // solve position
    // k1
    Eigen::Vector3d k1_p = v_0 * dt;

    // k2
    Eigen::Vector3d k2_p = v_1 * dt;

    // k3
    Eigen::Vector3d k3_p = v_2 * dt;

    // k4
    Eigen::Vector3d k4_p = v_3 * dt;

        cc.k1q = k1_q;
        cc.k1p = k1_p;
        cc.k1v = k1_v;

        cc.k2q = k2_q;
        cc.k2p = k2_p;
        cc.k2v = k2_v;

        cc.k3q = k3_q;
        cc.k3p = k3_p;
        cc.k3v = k3_v;

        cc.k4q = k4_q;
        cc.k4p = k4_p;
        cc.k4v = k4_v;


    // y+dt ================
    StatePlus state_plus;
    ProperQuaterniond dq = solve(dq_0, k1_q, k2_q, k3_q, k4_q);
    dq.normalize();
    state_plus.orientation = dq * q_0;
    state_plus.position = solve(p_0, k1_p, k2_p, k3_p, k4_p);
    state_plus.velocity = solve(v_0, k1_v, k2_v, k3_v, k4_v);

    return state_plus;
}


} // namespace ILLIXR