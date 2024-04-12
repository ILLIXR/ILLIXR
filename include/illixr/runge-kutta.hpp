#pragma once
#include "ProperQuaternion.hpp"

#include <eigen3/Eigen/Dense>
#include <iostream>

namespace ILLIXR {

const ProperQuaterniond dq_0(1., 0., 0., 0.);    /** Initial quaternion*/
const Eigen::Vector3d   Gravity(0.0, 0.0, 9.81); /** Gravitational acceleration, at sea level, on Earth*/

/**
 * @brief Generates a skew-symmetric matrix from the given 3-element vector
 *
 * Based on equation 6 from http://mars.cs.umn.edu/tr/reports/Trawny05b.pdf
 */
inline Eigen::Matrix3d symmetric_skew(const Eigen::Vector3d& vec) {
    Eigen::Matrix3d skew_q;
    skew_q << 0, -vec[2], vec[1], vec[2], 0, -vec[0], -vec[1], vec[0], 0;
    return skew_q;
}

/**
 * @brief Generate the Omega matrix from the input 3-element vector
 *
 * Based on equation 48 of http://mars.cs.umn.edu/tr/reports/Trawny05b.pdf
 */
inline Eigen::Matrix4d makeOmega(const Eigen::Vector3d& w) {
    Eigen::Matrix4d omega   = Eigen::Matrix4d::Zero();
    omega.block<3, 3>(0, 0) = -symmetric_skew(w);
    omega.block<1, 3>(3, 0) = -w.transpose();
    omega.block<3, 1>(0, 3) = w;
    return omega;
}

/**
 * Calculate the change in orientation based on the input Quaternion
 * @param k_n The input Quaternion
 * @return The change in orientation
 */
inline ProperQuaterniond delta_q(const ProperQuaterniond& k_n) {
    ProperQuaterniond dq(dq_0 + 0.5 * k_n);
    dq.normalize();
    return dq;
}

/**
 * f(x) for the orientation quaternion
 * @param av Vector representing the linear acceleration
 * @param dq The current orientation represented by a quaternion
 * @return The updated quaternion as a new instance
 */
inline ProperQuaterniond q_dot(const Eigen::Vector3d& av, const ProperQuaterniond& dq) {
    return ProperQuaterniond(Eigen::Vector4d(0.5 * makeOmega(av) * dq.asVector()));
}

/**
 * @brief f(x) for the position
 * Calculate the new position based on the initial velocity and change in velocity
 * @param iv The initial velocity as a vector
 * @param k_n The change in velocity as a vector
 * @return The updated position
 */
inline Eigen::Vector3d p_dot(const Eigen::Vector3d& iv, const Eigen::Vector3d& k_n) {
    return Eigen::Vector3d(iv + 0.5 * k_n);
}

/**
 * @brief f(x0 for the velocity
 * Calculate the updated velocity from the acceleration and initial and delta quaternions
 * @param dq The delta quaternion (change)
 * @param q The initial quaternion
 * @param l_acc The acceleration as a vector
 * @return The calculated velocity as a vector
 */
inline Eigen::Vector3d v_dot(const ProperQuaterniond& dq, const ProperQuaterniond& q, const Eigen::Vector3d& l_acc) {
    ProperQuaterniond temp = q * dq;
    temp.normalize();
    return temp.toRotationMatrix() * l_acc - Gravity;
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

/**
 * Convenience struct
 */
struct StatePlus {
    ProperQuaterniond orientation;
    Eigen::Vector3d   velocity;
    Eigen::Vector3d   position;

    [[maybe_unused]] StatePlus(const ProperQuaterniond& pq, const Eigen::Vector3d& vel, const Eigen::Vector3d& pos)
        : orientation(pq)
        , velocity(vel)
        , position(pos) { }

    [[maybe_unused]] StatePlus(const Eigen::Quaterniond& pq, const Eigen::Vector3d& vel, const Eigen::Vector3d& pos)
        : orientation(pq)
        , velocity(vel)
        , position(pos) { }

    StatePlus() = default;
};

std::ostream& operator<<(std::ostream& os, const StatePlus& sp) {
    os << "Quat " << sp.orientation << std::endl;
    os << "Vel  " << sp.velocity << std::endl;
    os << "Pos  " << sp.position << std::endl;
    return os;
}

/**
 * Calculate the updated state (orientation, position, and velocity) based on the initial and final
 * velocities and accelerations
 * @param dt The time between initial and final states (time step)
 * @param sp The initial state
 * @param ang_vel The initial angular velocity
 * @param linear_acc The initial linear acceleration
 * @param ang_vel2 The final angular velocity
 * @param linear_acc2 The final angular acceleration
 * @return The final state
 */
StatePlus predict_mean_rk4(double dt, const StatePlus& sp, const Eigen::Vector3d& ang_vel, const Eigen::Vector3d& linear_acc,
                           const Eigen::Vector3d& ang_vel2, const Eigen::Vector3d& linear_acc2) {
    Eigen::Vector3d       av       = ang_vel;
    Eigen::Vector3d       la       = linear_acc;
    const Eigen::Vector3d delta_av = (ang_vel2 - ang_vel) / dt;
    const Eigen::Vector3d delta_la = (linear_acc2 - linear_acc) / dt;

    // y0 ================
    ProperQuaterniond q_0 = sp.orientation; // initial orientation quaternion
    Eigen::Vector3d   p_0 = sp.position;    // initial position vector
    Eigen::Vector3d   v_0 = sp.velocity;    // initial velocity vector

    // Calculate the RK4 coefficients
    // solve orientation
    // k1
    ProperQuaterniond k1_q = q_dot(av, dq_0) * dt;
    av += 0.5 * delta_av * dt;
    // k2
    ProperQuaterniond dq_1 = delta_q(k1_q);
    ProperQuaterniond k2_q = q_dot(av, dq_1) * dt;
    // k3
    ProperQuaterniond dq_2 = delta_q(k2_q);
    ProperQuaterniond k3_q = q_dot(av, dq_2) * dt;
    // k4
    av += 0.5 * delta_av * dt;
    ProperQuaterniond dq_3 = delta_q(2. * k3_q);
    ProperQuaterniond k4_q = q_dot(av, dq_3) * dt;

    // solve velocity
    // k1
    Eigen::Vector3d k1_v = v_dot(dq_0, q_0, la) * dt;
    // k2
    la += 0.5 * delta_la * dt;
    Eigen::Vector3d k2_v = v_dot(dq_1, q_0, la) * dt;
    // k3
    Eigen::Vector3d k3_v = v_dot(dq_2, q_0, la) * dt;
    // k4
    la += 0.5 * delta_la * dt;
    Eigen::Vector3d k4_v = v_dot(dq_3, q_0, la) * dt;

    // solve position
    // k1
    Eigen::Vector3d k1_p = v_0 * dt;
    // k2
    Eigen::Vector3d k2_p = p_dot(v_0, k1_v) * dt;
    // k3
    Eigen::Vector3d k3_p = p_dot(v_0, k2_v) * dt;
    // k4
    Eigen::Vector3d k4_p = p_dot(v_0, 2. * k3_v) * dt;

    // y+dt ================
    StatePlus         state_plus;
    ProperQuaterniond dq = solve(dq_0, k1_q, k2_q, k3_q, k4_q);
    dq.normalize();
    state_plus.orientation = q_0 * dq;
    state_plus.orientation.normalize();
    state_plus.position = solve(p_0, k1_p, k2_p, k3_p, k4_p);
    state_plus.velocity = solve(v_0, k1_v, k2_v, k3_v, k4_v);
    return state_plus;
}

} // namespace ILLIXR