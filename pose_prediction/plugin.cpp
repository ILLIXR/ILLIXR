#include <shared_mutex>
#include <eigen3/Eigen/Dense>
#include "common/phonebook.hpp"
#include "common/pose_prediction.hpp"
#include "common/data_format.hpp"
#include "common/plugin.hpp"

using namespace ILLIXR;

class pose_prediction_impl : public pose_prediction {
public:
    pose_prediction_impl(const phonebook* const pb)
        : sb{pb->lookup_impl<switchboard>()}
        , _m_slow_pose{sb->get_reader<pose_type>("slow_pose")}
        , _m_imu_raw{sb->get_reader<imu_raw_type>("imu_raw")}
        , _m_true_pose{sb->get_reader<pose_type>("true_pose")}
        , _m_ground_truth_offset{sb->get_reader<switchboard::event_wrapper<<Eigen::Vector3f>>("ground_truth_offset")}
		, _m_vsync_estimate{sb->get_reader<switchboard::event_wrapper<time_type>>("vsync_estimate")}
    { }

    // No parameter get_fast_pose() should just predict to the next vsync
    // However, we don't have vsync estimation yet.
    // So we will predict to `now()`, as a temporary approximation
    virtual fast_pose_type get_fast_pose() const override {
        switchboard::ptr<const switchboard::event_wrapper<time_type>> vsync_estimate = _m_vsync_estimate.get_ro_nullable();

        if (vsync_estimate == nullptr) {
            return get_fast_pose(std::chrono::high_resolution_clock::now());
        } else {
            return get_fast_pose(*vsync_estimate);
        }
    }

    virtual pose_type get_true_pose() const override {
        switchboard::ptr<const pose_type> pose_ptr = _m_true_pose.get_ro_nullable();
        return correct_pose(
            pose_ptr ? *pose_ptr : pose_type{
                std::chrono::system_clock::now(),
                Eigen::Vector3f{0, 0, 0},
                Eigen::Quaternionf{1, 0, 0, 0},
            }
        );
    }

	virtual pose_type get_true_pose() const override {
		const auto * const pose = _m_true_pose->get_latest_ro();
		const auto * const offset = _m_ground_truth_offset->get_latest_ro();
		pose_type offset_pose;

		// Subtract offset if valid pose and offset, otherwise use zero pose.
		// Checking that pose and offset are both valid is safer than just
		// checking one or the other because it assumes nothing about the
		// ordering of writes on the producer's end or about the producer
		// actually writing to both streams.
		if (pose && offset) {
			offset_pose = *pose;
			offset_pose.position -= *offset;
		} else {
			offset_pose.sensor_time = std::chrono::system_clock::now();
			offset_pose.position = Eigen::Vector3f{0, 0, 0};
			offset_pose.orientation = Eigen::Quaternionf{1, 0, 0, 0};
		}

		return correct_pose(offset_pose);
	}

    // future_time: An absolute timepoint in the future
    virtual fast_pose_type get_fast_pose(time_type future_timestamp) const override {
        switchboard::ptr<const pose_type> slow_pose = _m_slow_pose.get_ro_nullable();
        if (!slow_pose) {
            // No slow pose, return 0
            return fast_pose_type{
                correct_pose(pose_type{}),
                std::chrono::system_clock::now(),
                future_timestamp,
            };
        }

        switchboard::ptr<const imu_raw_type> imu_raw = _m_imu_raw.get_ro_nullable();
        if (!imu_raw) {
#ifndef NDEBUG
            printf("FAST POSE IS SLOW POSE!");
#endif
            // No imu_raw, return slow_pose
            return fast_pose_type{
                .pose = correct_pose(*slow_pose),
                .predict_computed_time = std::chrono::system_clock::now(),
                .predict_target_time = future_timestamp,
            };
        }

        // slow_pose and imu_raw, do pose prediction

        double dt = std::chrono::duration_cast<std::chrono::nanoseconds>(future_timestamp - std::chrono::system_clock::now()).count();
        std::pair<Eigen::Matrix<double,13,1>, time_type> predictor_result = predict_mean_rk4(dt/NANO_SEC);

        auto state_plus = predictor_result.first;

        // predictor_imu_time is the most recent IMU sample that was used to compute the prediction.
        auto predictor_imu_time = predictor_result.second;
        
        pose_type predicted_pose = correct_pose({
            predictor_imu_time,
            Eigen::Vector3f{static_cast<float>(state_plus(4)), static_cast<float>(state_plus(5)), static_cast<float>(state_plus(6))}, 
            Eigen::Quaternionf{static_cast<float>(state_plus(3)), static_cast<float>(state_plus(0)), static_cast<float>(state_plus(1)), static_cast<float>(state_plus(2))}
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
        //       - the prediction compute time (time when this prediction was computed, i.e., now)
        //       - the prediction target (the time that was requested for this pose.)
        return fast_pose_type {
            .pose = predicted_pose,
            .predict_computed_time = std::chrono::high_resolution_clock::now(),
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
        //return _m_true_pose.valid();
        /*
          We do not have a "ground truth" available in all cases, such
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
    switchboard::reader<pose_type> _m_slow_pose;
    switchboard::reader<imu_raw_type> _m_imu_raw;
	switchboard::reader<pose_type> _m_true_pose;
    switchboard::reader<switchboard::event_wrapper<Eigen::Vector3f>> _m_ground_truth_offset;
    switchboard::reader<switchboard::event_wrapper<time_type>> _m_vsync_estimate;
	mutable Eigen::Quaternionf offset {Eigen::Quaternionf::Identity()};
	mutable std::shared_mutex offset_mutex;
    

    // Slightly modified copy of OpenVINS method found in propagator.cpp
    // Returns a pair of the predictor state_plus and the time associated with the
    // most recent imu reading used to perform this prediction.
    std::pair<Eigen::Matrix<double,13,1>,time_type> predict_mean_rk4(double dt) const {

        // Pre-compute things
        switchboard::ptr<const imu_raw_type> imu_raw = _m_imu_raw.get();

        Eigen::Vector3d w_hat =imu_raw->w_hat;
        Eigen::Vector3d a_hat = imu_raw->a_hat;
        Eigen::Vector3d w_alpha = (imu_raw->w_hat2-imu_raw->w_hat)/dt;
        Eigen::Vector3d a_jerk = (imu_raw->a_hat2-imu_raw->a_hat)/dt;

        // y0 ================
        Eigen::Quaterniond temp_quat = imu_raw->quat;
        Eigen::Vector4d q_0 = {temp_quat.x(), temp_quat.y(), temp_quat.z(), temp_quat.w()};
        Eigen::Vector3d p_0 = imu_raw->pos;
        Eigen::Vector3d v_0 = imu_raw->vel;

        // k1 ================
        Eigen::Vector4d dq_0 = {0,0,0,1};
        Eigen::Vector4d q0_dot = 0.5*Omega(w_hat)*dq_0;
        Eigen::Vector3d p0_dot = v_0;
        Eigen::Matrix3d R_Gto0 = quat_2_Rot(quat_multiply(dq_0,q_0));
        Eigen::Vector3d v0_dot = R_Gto0.transpose()*a_hat-Eigen::Vector3d{0.0, 0.0, 9.81};

        Eigen::Vector4d k1_q = q0_dot*dt;
        Eigen::Vector3d k1_p = p0_dot*dt;
        Eigen::Vector3d k1_v = v0_dot*dt;

        // k2 ================
        w_hat += 0.5*w_alpha*dt;
        a_hat += 0.5*a_jerk*dt;

        Eigen::Vector4d dq_1 = quatnorm(dq_0+0.5*k1_q);
        Eigen::Vector3d v_1 = v_0+0.5*k1_v;

        Eigen::Vector4d q1_dot = 0.5*Omega(w_hat)*dq_1;
        Eigen::Vector3d p1_dot = v_1;
        Eigen::Matrix3d R_Gto1 = quat_2_Rot(quat_multiply(dq_1,q_0));
        Eigen::Vector3d v1_dot = R_Gto1.transpose()*a_hat-Eigen::Vector3d{0.0, 0.0, 9.81};

        Eigen::Vector4d k2_q = q1_dot*dt;
        Eigen::Vector3d k2_p = p1_dot*dt;
        Eigen::Vector3d k2_v = v1_dot*dt;

        // k3 ================
        Eigen::Vector4d dq_2 = quatnorm(dq_0+0.5*k2_q);
        //Eigen::Vector3d p_2 = p_0+0.5*k2_p;
        Eigen::Vector3d v_2 = v_0+0.5*k2_v;

        Eigen::Vector4d q2_dot = 0.5*Omega(w_hat)*dq_2;
        Eigen::Vector3d p2_dot = v_2;
        Eigen::Matrix3d R_Gto2 = quat_2_Rot(quat_multiply(dq_2,q_0));
        Eigen::Vector3d v2_dot = R_Gto2.transpose()*a_hat-Eigen::Vector3d{0.0, 0.0, 9.81};

        Eigen::Vector4d k3_q = q2_dot*dt;
        Eigen::Vector3d k3_p = p2_dot*dt;
        Eigen::Vector3d k3_v = v2_dot*dt;

        // k4 ================
        w_hat += 0.5*w_alpha*dt;
        a_hat += 0.5*a_jerk*dt;

        Eigen::Vector4d dq_3 = quatnorm(dq_0+k3_q);
        //Eigen::Vector3d p_3 = p_0+k3_p;
        Eigen::Vector3d v_3 = v_0+k3_v;

        Eigen::Vector4d q3_dot = 0.5*Omega(w_hat)*dq_3;
        Eigen::Vector3d p3_dot = v_3;
        Eigen::Matrix3d R_Gto3 = quat_2_Rot(quat_multiply(dq_3,q_0));
        Eigen::Vector3d v3_dot = R_Gto3.transpose()*a_hat-Eigen::Vector3d{0.0, 0.0, 9.81};

        Eigen::Vector4d k4_q = q3_dot*dt;
        Eigen::Vector3d k4_p = p3_dot*dt;
        Eigen::Vector3d k4_v = v3_dot*dt;

        // y+dt ================
        Eigen::Matrix<double,13,1> state_plus = Eigen::Matrix<double,13,1>::Zero();
        Eigen::Vector4d dq = quatnorm(dq_0+(1.0/6.0)*k1_q+(1.0/3.0)*k2_q+(1.0/3.0)*k3_q+(1.0/6.0)*k4_q);
        state_plus.block(0,0,4,1) = quat_multiply(dq, q_0);
        state_plus.block(4,0,3,1) = p_0+(1.0/6.0)*k1_p+(1.0/3.0)*k2_p+(1.0/3.0)*k3_p+(1.0/6.0)*k4_p;
        state_plus.block(7,0,3,1) = v_0+(1.0/6.0)*k1_v+(1.0/3.0)*k2_v+(1.0/3.0)*k3_v+(1.0/6.0)*k4_v;

        return {state_plus, imu_raw->imu_time};
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
        w_x << 0, -w(2), w(1),
                w(2), 0, -w(0),
                -w(1), w(0), 0;
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
     * @brief Multiply two JPL quaternions
     *
     * This is based on equation 9 in [Indirect Kalman Filter for 3D Attitude Estimation](http://mars.cs.umn.edu/tr/reports/Trawny05b.pdf).
     * We also enforce that the quaternion is unique by having q_4 be greater than zero.
     * \f{align*}{
     *  \bar{q}\otimes\bar{p}=
     *  \mathcal{L}(\bar{q})\bar{p}=
     *  \begin{bmatrix}
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
    static const inline Eigen::Matrix<double, 4, 1> quat_multiply(const Eigen::Matrix<double, 4, 1> &q, const Eigen::Matrix<double, 4, 1> &p) {
        Eigen::Matrix<double, 4, 1> q_t;
        Eigen::Matrix<double, 4, 4> Qm;
        // create big L matrix
        Qm.block(0, 0, 3, 3) = q(3, 0) * Eigen::MatrixXd::Identity(3, 3) - skew_x(q.block(0, 0, 3, 1));
        Qm.block(0, 3, 3, 1) = q.block(0, 0, 3, 1);
        Qm.block(3, 0, 1, 3) = -q.block(0, 0, 3, 1).transpose();
        Qm(3, 3) = q(3, 0);
        q_t = Qm * p;
        // ensure unique by forcing q_4 to be >0
        if (q_t(3, 0) < 0) {
            q_t *= -1;
        }
        // normalize and return
        return q_t / q_t.norm();
    }
};

class pose_prediction_plugin : public plugin {
public:
    pose_prediction_plugin(const std::string& name, phonebook* pb)
        : plugin{name, pb}
    {
        pb->register_impl<pose_prediction>(
            std::static_pointer_cast<pose_prediction>(
                std::make_shared<pose_prediction_impl>(pb)
            )
        );
    }
};

PLUGIN_MAIN(pose_prediction_plugin);
