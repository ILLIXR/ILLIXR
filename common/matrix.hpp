#pragma once

#include <eigen3/Eigen/Dense>

/**
 * @brief Normalizes a quaternion to make sure it is unit norm
 * @param q_t Quaternion to normalized
 * @return Normalized quaterion
 */
static const inline Eigen::Matrix<double, 4, 1> quatnorm(Eigen::Matrix<double, 4, 1> q_t) {
    if (q_t(3, 0) < 0) {
        q_t *= -1;
    }
    return q_t / q_t.norm();
}

/**
 * @brief Skew-symmetric matrix from a given 3x1 vector
 *
 * This is based on equation 6 in [Indirect Kalman Filter for 3D Attitude
 * Estimation](http://mars.cs.umn.edu/tr/reports/Trawny05b.pdf): \f{align*}{ \lfloor\mathbf{v}\times\rfloor =
 *  \begin{bmatrix}
 *  0 & -v_3 & v_2 \\ v_3 & 0 & -v_1 \\ -v_2 & v_1 & 0
 *  \end{bmatrix}
 * @f}
 *
 * @param[in] w 3x1 vector to be made a skew-symmetric
 * @return 3x3 skew-symmetric matrix
 */
static const inline Eigen::Matrix<double, 3, 3> skew_x(const Eigen::Matrix<double, 3, 1>& w) {
    Eigen::Matrix<double, 3, 3> w_x;
    w_x << 0, -w(2), w(1), w(2), 0, -w(0), -w(1), w(0), 0;
    return w_x;
}

/**
 * @brief Converts JPL quaterion to SO(3) rotation matrix
 *
 * This is based on equation 62 in [Indirect Kalman Filter for 3D Attitude
 * Estimation](http://mars.cs.umn.edu/tr/reports/Trawny05b.pdf): \f{align*}{ \mathbf{R} =
 * (2q_4^2-1)\mathbf{I}_3-2q_4\lfloor\mathbf{q}\times\rfloor+2\mathbf{q}^\top\mathbf{q}
 * @f}
 *
 * @param[in] q JPL quaternion
 * @return 3x3 SO(3) rotation matrix
 */
static const inline Eigen::Matrix<double, 3, 3> quat_2_Rot(const Eigen::Matrix<double, 4, 1>& q) {
    Eigen::Matrix<double, 3, 3> q_x = skew_x(q.block(0, 0, 3, 1));
    Eigen::MatrixXd             Rot = (2 * std::pow(q(3, 0), 2) - 1) * Eigen::MatrixXd::Identity(3, 3) - 2 * q(3, 0) * q_x +
        2 * q.block(0, 0, 3, 1) * (q.block(0, 0, 3, 1).transpose());
    return Rot;
}

/**
 * @brief Returns a JPL quaternion from a rotation matrix
 *
 * This is based on the equation 74 in [Indirect Kalman Filter for 3D Attitude Estimation](http://mars.cs.umn.edu/tr/reports/Trawny05b.pdf).
 * In the implementation, we have 4 statements so that we avoid a division by zero and
 * instead always divide by the largest diagonal element. This all comes from the
 * definition of a rotation matrix, using the diagonal elements and an off-diagonal.
 * \f{align*}{
 *  \mathbf{R}(\bar{q})=
 *  \begin{bmatrix}
 *  q_1^2-q_2^2-q_3^2+q_4^2 & 2(q_1q_2+q_3q_4) & 2(q_1q_3-q_2q_4) \\
 *  2(q_1q_2-q_3q_4) & -q_2^2+q_2^2-q_3^2+q_4^2 & 2(q_2q_3+q_1q_4) \\
 *  2(q_1q_3+q_2q_4) & 2(q_2q_3-q_1q_4) & -q_1^2-q_2^2+q_3^2+q_4^2
 *  \end{bmatrix}
 * \f}
 *
 * @param[in] rot 3x3 rotation matrix
 * @return 4x1 quaternion
 */
static const inline Eigen::Matrix<double, 4, 1> rot_2_quat(const Eigen::Matrix<double, 3, 3> &rot) {
    Eigen::Matrix<double, 4, 1> q;
    double T = rot.trace();
    if ((rot(0, 0) >= T) && (rot(0, 0) >= rot(1, 1)) && (rot(0, 0) >= rot(2, 2))) {
        q(0) = sqrt((1 + (2 * rot(0, 0)) - T) / 4);
        q(1) = (1 / (4 * q(0))) * (rot(0, 1) + rot(1, 0));
        q(2) = (1 / (4 * q(0))) * (rot(0, 2) + rot(2, 0));
        q(3) = (1 / (4 * q(0))) * (rot(1, 2) - rot(2, 1));

    } else if ((rot(1, 1) >= T) && (rot(1, 1) >= rot(0, 0)) && (rot(1, 1) >= rot(2, 2))) {
        q(1) = sqrt((1 + (2 * rot(1, 1)) - T) / 4);
        q(0) = (1 / (4 * q(1))) * (rot(0, 1) + rot(1, 0));
        q(2) = (1 / (4 * q(1))) * (rot(1, 2) + rot(2, 1));
        q(3) = (1 / (4 * q(1))) * (rot(2, 0) - rot(0, 2));
    } else if ((rot(2, 2) >= T) && (rot(2, 2) >= rot(0, 0)) && (rot(2, 2) >= rot(1, 1))) {
        q(2) = sqrt((1 + (2 * rot(2, 2)) - T) / 4);
        q(0) = (1 / (4 * q(2))) * (rot(0, 2) + rot(2, 0));
        q(1) = (1 / (4 * q(2))) * (rot(1, 2) + rot(2, 1));
        q(3) = (1 / (4 * q(2))) * (rot(0, 1) - rot(1, 0));
    } else {
        q(3) = sqrt((1 + T) / 4);
        q(0) = (1 / (4 * q(3))) * (rot(1, 2) - rot(2, 1));
        q(1) = (1 / (4 * q(3))) * (rot(2, 0) - rot(0, 2));
        q(2) = (1 / (4 * q(3))) * (rot(0, 1) - rot(1, 0));
    }
    if (q(3) < 0) {
        q = -q;
    }
    // normalize and return
    q = q / (q.norm());
    return q;
}


/**
 * @brief Multiply two JPL quaternions
 *
 * This is based on equation 9 in [Indirect Kalman Filter for 3D Attitude
 * Estimation](http://mars.cs.umn.edu/tr/reports/Trawny05b.pdf). We also enforce that the quaternion is unique by having q_4
 * be greater than zero. \f{align*}{ \bar{q}\otimes\bar{p}= \mathcal{L}(\bar{q})\bar{p}= \begin{bmatrix}
 *  q_4\mathbf{I}_3+\lfloor\mathbf{q}\times\rfloor & \mathbf{q} \\
 *  -\mathbf{q}^\top & q_4
 *  \end{bmatrix}
 *  \begin{bmatrix}
 *  \mathbf{p} \\ p_4
 *  \end{bmatrix}
 * @f}
 *
 * @param[in] q First JPL quaternion
 * @param[in] p Second JPL quaternion
 * @return 4x1 resulting p*q quaternion
 */
static const inline Eigen::Matrix<double, 4, 1> quat_multiply(const Eigen::Matrix<double, 4, 1>& q,
                                                                const Eigen::Matrix<double, 4, 1>& p) {
    Eigen::Matrix<double, 4, 1> q_t;
    Eigen::Matrix<double, 4, 4> Qm;
    // create big L matrix
    Qm.block(0, 0, 3, 3) = q(3, 0) * Eigen::MatrixXd::Identity(3, 3) - skew_x(q.block(0, 0, 3, 1));
    Qm.block(0, 3, 3, 1) = q.block(0, 0, 3, 1);
    Qm.block(3, 0, 1, 3) = -q.block(0, 0, 3, 1).transpose();
    Qm(3, 3)             = q(3, 0);
    q_t                  = Qm * p;
    // ensure unique by forcing q_4 to be >0
    if (q_t(3, 0) < 0) {
        q_t *= -1;
    }
    // normalize and return
    return q_t / q_t.norm();
}

/**
 * @brief SO(3) matrix exponential
 *
 * SO(3) matrix exponential mapping from the vector to SO(3) lie group.
 * This formula ends up being the [Rodrigues formula](https://en.wikipedia.org/wiki/Rodrigues%27_rotation_formula).
 * This definition was taken from "Lie Groups for 2D and 3D Transformations" by Ethan Eade equation 15.
 * http://ethaneade.com/lie.pdf
 *
 * \f{align*}{
 * \exp\colon\mathfrak{so}(3)&\to SO(3) \\
 * \exp(\mathbf{v}) &=
 * \mathbf{I}
 * +\frac{\sin{\theta}}{\theta}\lfloor\mathbf{v}\times\rfloor
 * +\frac{1-\cos{\theta}}{\theta^2}\lfloor\mathbf{v}\times\rfloor^2 \\
 * \mathrm{where}&\quad \theta^2 = \mathbf{v}^\top\mathbf{v}
 * @f}
 *
 * @param[in] w 3x1 vector we will take the exponential of
 * @return SO(3) rotation matrix
 */
static const inline Eigen::Matrix<double, 3, 3> exp_so3(const Eigen::Matrix<double, 3, 1> &w) {
    // get theta
    Eigen::Matrix<double, 3, 3> w_x = skew_x(w);
    double theta = w.norm();
    // Handle small angle values
    double A, B;
    if(theta < 1e-12) {
        A = 1;
        B = 0.5;
    } else {
        A = sin(theta)/theta;
        B = (1-cos(theta))/(theta*theta);
    }
    // compute so(3) rotation
    Eigen::Matrix<double, 3, 3> R;
    if (theta == 0) {
        R = Eigen::MatrixXd::Identity(3, 3);
    } else {
        R = Eigen::MatrixXd::Identity(3, 3) + A * w_x + B * w_x * w_x;
    }
    return R;
}

// Slightly modified copy of OpenVINS method found in propagator.cpp
// Returns a pair of the predictor state and the time associated with the
// most recent imu reading used to perform this prediction.
/**
 * @brief Integrated quaternion from angular velocity
 *
 * See equation (48) of trawny tech report [Indirect Kalman Filter for 3D Attitude
 * Estimation](http://mars.cs.umn.edu/tr/reports/Trawny05b.pdf).
 *
 */
static const inline Eigen::Matrix<double, 4, 4> Omega(Eigen::Matrix<double, 3, 1> w) {
    Eigen::Matrix<double, 4, 4> mat;
    mat.block(0, 0, 3, 3) = -skew_x(w);
    mat.block(3, 0, 1, 3) = -w.transpose();
    mat.block(0, 3, 3, 1) = w;
    mat(3, 3)             = 0;
    return mat;
}