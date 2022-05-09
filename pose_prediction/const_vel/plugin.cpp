#include <shared_mutex>
#include <eigen3/Eigen/Dense>
#include "common/phonebook.hpp"
#include "common/pose_prediction.hpp"
#include "common/data_format.hpp"
#include "common/plugin.hpp"

using namespace ILLIXR;

class const_vel_predictor_impl : public pose_prediction {
public:
    const_vel_predictor_impl(const phonebook* const pb)
        : sb{pb->lookup_impl<switchboard>()}
        , _m_clock{pb->lookup_impl<RelativeClock>()}
        , _m_slow_pose{sb->get_reader<pose_type>("slow_pose")}
        , _m_imu_raw{sb->get_reader<imu_raw_type>("imu_raw")}
        , _m_true_pose{sb->get_reader<pose_type>("true_pose")}
        , _m_ground_truth_offset{sb->get_reader<switchboard::event_wrapper<Eigen::Vector3f>>("ground_truth_offset")}
        , _m_vsync_estimate{sb->get_reader<switchboard::event_wrapper<time_point>>("vsync_estimate")}
    { }

    // No parameter get_fast_pose() predicts to the next vsync estimate, if one exists, otherwise predicts to 'now'
    virtual fast_pose_type get_fast_pose() const override {
        switchboard::ptr<const switchboard::event_wrapper<time_point>> vsync_estimate = _m_vsync_estimate.get_ro_nullable();

        if (vsync_estimate == nullptr) {
            return get_fast_pose(_m_clock->now());
        } else {
            return get_fast_pose(*vsync_estimate);
        }
    }

    virtual pose_type get_true_pose() const override {
        switchboard::ptr<const pose_type> pose_ptr = _m_true_pose.get_ro_nullable();
        switchboard::ptr<const switchboard::event_wrapper<Eigen::Vector3f>> offset_ptr = _m_ground_truth_offset.get_ro_nullable();

        pose_type offset_pose;

        // Subtract offset if valid pose and offset, otherwise use zero pose.
        // Checking that pose and offset are both valid is safer than just
        // checking one or the other because it assumes nothing about the
        // ordering of writes on the producer's end or about the producer
        // actually writing to both streams.
        if (pose_ptr != nullptr && offset_ptr != nullptr) {
            offset_pose             = *pose_ptr;
            offset_pose.position   -= **offset_ptr;
        } else {
            offset_pose.sensor_time = _m_clock->now();
            offset_pose.position    = Eigen::Vector3f{0, 0, 0};
            offset_pose.orientation = Eigen::Quaternionf{1, 0, 0, 0};
        }

        return correct_pose(offset_pose);
    }


    // future_time: An absolute timepoint in the future
    virtual fast_pose_type get_fast_pose(time_point future_timestamp) const override {
        switchboard::ptr<const pose_type> slow_pose = _m_slow_pose.get_ro_nullable();
        if (slow_pose == nullptr) {
            // No slow pose, return identity pose
            return fast_pose_type{
                .pose = correct_pose(pose_type{}),
                .predict_computed_time = _m_clock->now(),
                .predict_target_time = future_timestamp,
            };
        }

        switchboard::ptr<const imu_raw_type> imu_raw = _m_imu_raw.get_ro_nullable();
        if (imu_raw == nullptr) {
#ifndef NDEBUG
            printf("Fast pose is slow pose!\n");
#endif
            // No imu_raw, return slow_pose
            return fast_pose_type{
                .pose = correct_pose(*slow_pose),
                .predict_computed_time = _m_clock->now(),
                .predict_target_time = future_timestamp,
            };
        }

        // Both slow_pose and imu_raw are available, do pose prediction
        double dt = duration2double(future_timestamp - imu_raw->imu_time);
        std::pair<Eigen::Matrix<double, 13, 1>, time_point> predictor_result = predict_const_vel(dt);
        auto state_plus = predictor_result.first;

        // predictor_imu_time is the most recent IMU sample that was used to compute the prediction.
        auto predictor_imu_time = predictor_result.second;

        pose_type predicted_pose = correct_pose({
            predictor_imu_time,
            Eigen::Vector3f{
                static_cast<float>(state_plus(4)),
                static_cast<float>(state_plus(5)),
                static_cast<float>(state_plus(6))
            },
            Eigen::Quaternionf{
                static_cast<float>(state_plus(3)),
                static_cast<float>(state_plus(0)),
                static_cast<float>(state_plus(1)),
                static_cast<float>(state_plus(2))
            }
        });

        // Make the first valid fast pose be straight ahead.
        if (first_time) {
            std::unique_lock lock {offset_mutex};
            // check again, now that we have mutual exclusion
            if (first_time) {
                first_time = false;
                offset = predicted_pose.orientation.inverse();
            }
        }

        // Several timestamps are logged:
        // - the prediction compute time (time when this prediction was computed, i.e., now)
        // - the prediction target (the time that was requested for this pose.)
        return fast_pose_type {
            .pose = predicted_pose,
            .predict_computed_time = _m_clock->now(),
            .predict_target_time = future_timestamp
        };
    }

    virtual void set_offset(const Eigen::Quaternionf& raw_o_times_offset) override {
        std::unique_lock lock {offset_mutex};
        Eigen::Quaternionf raw_o = raw_o_times_offset * offset.inverse();
        offset = raw_o.inverse();
        /*
          Now, `raw_o` is maps to the identity quaternion.
          Proof:
          apply_offset(raw_o)
              = raw_o * offset
              = raw_o * raw_o.inverse()
              = Identity.
         */
    }

    Eigen::Quaternionf apply_offset(const Eigen::Quaternionf& orientation) const {
        std::shared_lock lock {offset_mutex};
        return orientation * offset;
    }


    virtual bool fast_pose_reliable() const override {
        return _m_slow_pose.get_ro_nullable() && _m_imu_raw.get_ro_nullable();
        /*
          SLAM takes some time to initialize, so initially fast_pose
          is unreliable.

          In such cases, we might return a fast_pose based only on the
          IMU data (currently, we just return a zero-pose)., and mark
          it as "unreliable"

          This way, there always a pose coming out of pose_prediction,
          representing our best guess at that time, and we indicate
          how reliable that guess is here.

         */
    }

    virtual bool true_pose_reliable() const override {
        /*
          We do not have a ground truth available in all cases, such
          as when reading live data.
         */
        return bool(_m_true_pose.get_ro_nullable());
    }

    virtual Eigen::Quaternionf get_offset() override {
        return offset;
    }

    // Correct the orientation of the pose due to the lopsided IMU in the
    // current Dataset we are using (EuRoC)
    virtual pose_type correct_pose(const pose_type pose) const override {
        pose_type swapped_pose;

        // Make any changes to the axes direction below
        // This is a mapping between the coordinate system of the current
        // SLAM (OpenVINS) we are using and the OpenGL system.
        swapped_pose.position.x() = -pose.position.y();
        swapped_pose.position.y() = pose.position.z();
        swapped_pose.position.z() = -pose.position.x();

        // Make any chanes to orientation of the output below
        // For the dataset were currently using (EuRoC), the output orientation acts as though
        // the "top of the head" is the forward direction, and the "eye direction" is the up direction.
        Eigen::Quaternionf raw_o (pose.orientation.w(), -pose.orientation.y(), pose.orientation.z(), -pose.orientation.x());

        swapped_pose.orientation = apply_offset(raw_o);
        swapped_pose.sensor_time = pose.sensor_time;

        return swapped_pose;
    }

private:
    mutable std::atomic<bool> first_time{true};
    const std::shared_ptr<switchboard> sb;
    const std::shared_ptr<const RelativeClock> _m_clock;
    switchboard::reader<pose_type> _m_slow_pose;
    switchboard::reader<imu_raw_type> _m_imu_raw;
    switchboard::reader<pose_type> _m_true_pose;
    switchboard::reader<switchboard::event_wrapper<Eigen::Vector3f>> _m_ground_truth_offset;
    switchboard::reader<switchboard::event_wrapper<time_point>> _m_vsync_estimate;
    mutable Eigen::Quaternionf offset {Eigen::Quaternionf::Identity()};
    mutable std::shared_mutex offset_mutex;

    // Based on https://github.com/rpng/open_vins/issues/92
    // Returns a pair of the predictor state and the time associated with the
    // most recent imu reading used to perform this prediction.
    std::pair<Eigen::Matrix<double, 13, 1>, time_point> predict_const_vel(double dt) const {
        const auto imu_raw = _m_imu_raw.get_ro();

        // Current state
        const auto curr_time = imu_raw->imu_time;
        // TODO: should this be the last w or the estimated w in the VIO/integrator state?
        const auto curr_w    = 0.5 * (imu_raw->w_hat + imu_raw->w_hat2);
        const auto curr_quat = imu_raw->quat;
        const auto curr_R    = quat_2_Rot(Eigen::Matrix<double, 4, 1>{curr_quat.x(), curr_quat.y(), curr_quat.z(), curr_quat.w()});
        const auto curr_pos  = imu_raw->pos;
        const auto curr_vel  = imu_raw->vel;

        // Compute new quaternion

        // Option 1: PG's code from issue 92. Same as Propagator.cpp
        const auto future_q = rot_2_quat(exp_so3(-curr_w * dt) * curr_R);

        // Option 2: PG's formula from issue 92. Same as Trawny eq. 96.
        // const auto future_q = rot_2_quat(exp_so3(0.5 * Omega(curr_w) * dt) * curr_R);

        // Compute new position
        const auto delta_pos = curr_vel * dt;
        const auto future_pos = curr_pos + delta_pos;

        // Assemble state
        Eigen::Matrix<double, 13, 1> state_plus = Eigen::Matrix<double, 13, 1>::Zero();
        state_plus.block(0, 0, 4, 1) = future_q;
        state_plus.block(4, 0, 3, 1) = future_pos;
        state_plus.block(7, 0, 3, 1) = curr_vel;

        return {state_plus, curr_time};
    }

    /**
     * @brief Integrated quaternion from angular velocity
     *
     * See equation (48) of trawny tech report [Indirect Kalman Filter for 3D Attitude Estimation](http://mars.cs.umn.edu/tr/reports/Trawny05b.pdf).
     *
     */
    static const inline Eigen::Matrix<double, 4, 4> Omega(Eigen::Matrix<double, 3, 1> w) {
        Eigen::Matrix<double, 4, 4> mat;
        mat.block(0, 0, 3, 3) = -skew_x(w);
        mat.block(3, 0, 1, 3) = -w.transpose();
        mat.block(0, 3, 3, 1) = w;
        mat(3, 3) = 0;
        return mat;
    }

    /**
     * @brief Skew-symmetric matrix from a given 3x1 vector
     *
     * This is based on equation 6 in [Indirect Kalman Filter for 3D Attitude Estimation](http://mars.cs.umn.edu/tr/reports/Trawny05b.pdf):
     * \f{align*}{
     *  \lfloor\mathbf{v}\times\rfloor =
     *  \begin{bmatrix}
     *  0 & -v_3 & v_2 \\ v_3 & 0 & -v_1 \\ -v_2 & v_1 & 0
     *  \end{bmatrix}
     * @f}
     *
     * @param[in] w 3x1 vector to be made a skew-symmetric
     * @return 3x3 skew-symmetric matrix
     */
    static const inline Eigen::Matrix<double, 3, 3> skew_x(const Eigen::Matrix<double, 3, 1> &w) {
        Eigen::Matrix<double, 3, 3> w_x;
        w_x <<     0, -w(2),  w(1),
                w(2),     0, -w(0),
               -w(1),  w(0),     0;
        return w_x;
    }

    /**
     * @brief Converts JPL quaterion to SO(3) rotation matrix
     *
     * This is based on equation 62 in [Indirect Kalman Filter for 3D Attitude Estimation](http://mars.cs.umn.edu/tr/reports/Trawny05b.pdf):
     * \f{align*}{
     *  \mathbf{R} = (2q_4^2-1)\mathbf{I}_3-2q_4\lfloor\mathbf{q}\times\rfloor+2\mathbf{q}^\top\mathbf{q}
     * @f}
     *
     * @param[in] q JPL quaternion
     * @return 3x3 SO(3) rotation matrix
     */
    static const inline Eigen::Matrix<double, 3, 3> quat_2_Rot(const Eigen::Matrix<double, 4, 1> &q) {
        Eigen::Matrix<double, 3, 3> q_x = skew_x(q.block(0, 0, 3, 1));
        Eigen::MatrixXd Rot = (2 * std::pow(q(3, 0), 2) - 1) * Eigen::MatrixXd::Identity(3, 3)
                              - 2 * q(3, 0) * q_x +
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
};

class const_vel_predictor_plugin : public plugin {
public:
    const_vel_predictor_plugin(const std::string& name, phonebook* pb)
        : plugin{name, pb}
    {
        pb->register_impl<pose_prediction>(
            std::static_pointer_cast<pose_prediction>(
                std::make_shared<const_vel_predictor_impl>(pb)
            )
        );
    }
};

PLUGIN_MAIN(const_vel_predictor_plugin);
