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
		, _m_slow_pose{sb->subscribe_latest<pose_type>("slow_pose")}
        , _m_imu_biases{sb->subscribe_latest<imu_biases_type>("imu_biases")}
        , _m_true_pose{sb->subscribe_latest<pose_type>("true_pose")}
        , _m_vsync_estimate{sb->subscribe_latest<time_type>("vsync_estimate")}
        , _m_start_of_time{std::chrono::high_resolution_clock::now()}
    { }

    virtual pose_type get_true_pose() override {
		const pose_type* pose_ptr = _m_true_pose->get_latest_ro();
		return correct_pose(
			pose_ptr ? *pose_ptr : pose_type{}
		);
    }

    // No paramter pose predict will just get the current slow pose based on the next vsync
    virtual fast_pose_type get_fast_pose() override {
        // const pose_type* pose_ptr = _m_slow_pose->get_latest_ro();
        // return correct_pose(
        //     pose_ptr ? *pose_ptr : pose_type{}
        // );
        const time_type *vsync_estimate = _m_vsync_estimate->get_latest_ro();

        if(vsync_estimate == nullptr) {
            return get_fast_pose(std::chrono::high_resolution_clock::now());
        } else {
            return get_fast_pose(*vsync_estimate);
        }
        

        // if (!vsync_estimate || std::chrono::system_clock::now() > *vsync_estimate) {
        //     time_type vsync = get_vsync();
        //     return get_fast_pose(vsync);
        // } else {
        //     return get_fast_pose(*vsync_estimate);
        // }
    }

    // future_time: Timestamp in the future in seconds
    virtual fast_pose_type get_fast_pose(time_type future_timestamp) override {

        // Generates a dummy yaw-back-and-forth pose.
        
        // double time = std::chrono::duration_cast<std::chrono::nanoseconds>(future_timestamp - _m_start_of_time).count();
        // time /= 1000000000.0f;
        // float yaw = std::cos(time);

        // Eigen::Quaternionf testQ = Eigen::AngleAxisf(yaw, Eigen::Vector3f::UnitX())
        //     * Eigen::AngleAxisf(0, Eigen::Vector3f::UnitY())
        //     * Eigen::AngleAxisf(0, Eigen::Vector3f::UnitZ());
        
        // pose_type* test_pose = new pose_type {
        //     future_timestamp,
        //     Eigen::Vector3f{static_cast<float>(0), static_cast<float>(std::cos(time)), static_cast<float>(0)}, 
        //     testQ
        // };
        // return fast_pose_type {
        //     .pose = correct_pose(*test_pose),
        //     .imu_time = std::chrono::high_resolution_clock::now(),
        //     .predict_computed_time = std::chrono::high_resolution_clock::now(),
        //     .predict_target_time = future_timestamp
        // };

        if (!_m_imu_biases->get_latest_ro()) {
            const pose_type* pose_ptr = _m_slow_pose->get_latest_ro();
            
            return fast_pose_type{
                .pose = correct_pose(pose_ptr ? *pose_ptr : pose_type{})
            };
        }
        double dt = std::chrono::duration_cast<std::chrono::nanoseconds>(future_timestamp - std::chrono::system_clock::now()).count();
        std::pair<Eigen::Matrix<double,13,1>, time_type> predictor_result = predict_mean_rk4(dt/NANO_SEC);

        auto state_plus = predictor_result.first;
        auto predictor_imu_time = predictor_result.second;
        
        pose_type* pose_ptr = new pose_type {
            .sensor_time = predictor_imu_time,
            .position = Eigen::Vector3f{static_cast<float>(state_plus(4)), static_cast<float>(state_plus(5)), static_cast<float>(state_plus(6))}, 
            .orientation = Eigen::Quaternionf{static_cast<float>(state_plus(3)), static_cast<float>(state_plus(0)), static_cast<float>(state_plus(1)), static_cast<float>(state_plus(2))}
        };

        // Several timestamps are logged:
        //       - the imu time (time when imu data was originally ingested for this prediction)
        //       - the prediction compute time (time when this prediction was computed, i.e., now)
        //       - the prediction target (the time that was requested for this pose.)
        return fast_pose_type {
            .pose = correct_pose(*pose_ptr),
            .imu_time = predictor_imu_time,
            .predict_computed_time = std::chrono::high_resolution_clock::now(),
            .predict_target_time = future_timestamp
        };
    }

	virtual void set_offset(const Eigen::Quaternionf& raw_o_times_offset) override {
		std::lock_guard<std::mutex> lock {offset_mutex};
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
		std::lock_guard<std::mutex> lock {offset_mutex};
		return orientation * offset;
	}


	virtual bool fast_pose_reliable() const override {
		//return _m_slow_pose.valid();
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
		return true;
	}

	virtual bool true_pose_reliable() const override {
		//return _m_true_pose.valid();
		/*
		  We do not have a "ground truth" available in all cases, such
		  as when reading live data.
		 */
		return true;
	}

private:
	const std::shared_ptr<switchboard> sb;
    std::unique_ptr<reader_latest<pose_type>> _m_slow_pose;
    std::unique_ptr<reader_latest<imu_biases_type>> _m_imu_biases;
	std::unique_ptr<reader_latest<pose_type>> _m_true_pose;
    std::unique_ptr<reader_latest<time_type>> _m_vsync_estimate;
    time_type _m_start_of_time;
	Eigen::Quaternionf offset {Eigen::Quaternionf::Identity()};
	mutable std::mutex offset_mutex;

    // Correct the orientation of the pose due to the lopsided IMU in the EuRoC Dataset
    pose_type correct_pose(const pose_type pose) const {
        pose_type swapped_pose;

        // This uses the OpenVINS standard output coordinate system.
        // This is a mapping between the OV coordinate system and the OpenGL system.
        swapped_pose.position.x() = -pose.position.y();
        swapped_pose.position.y() = pose.position.z();
        swapped_pose.position.z() = -pose.position.x();

		
        // There is a slight issue with the orientations: basically,
        // the output orientation acts as though the "top of the head" is the
        // forward direction, and the "eye direction" is the up direction.
		Eigen::Quaternionf raw_o (pose.orientation.w(), -pose.orientation.y(), pose.orientation.z(), -pose.orientation.x());

		swapped_pose.orientation = apply_offset(raw_o);
        swapped_pose.sensor_time = pose.sensor_time;

        return swapped_pose;
    }

    // Slightly modified copy of OpenVINS method found in propagator.cpp
    // Returns a pair of the predictor state_plus and the time associated with the
    // most recent imu reading used to perform this prediction.
    std::pair<Eigen::Matrix<double,13,1>,time_type> predict_mean_rk4(double dt) const {

        // Pre-compute things
        const imu_biases_type *imu_biases = _m_imu_biases->get_latest_ro();

        Eigen::Vector3d w_hat =imu_biases->w_hat;
        Eigen::Vector3d a_hat = imu_biases->a_hat;
        Eigen::Vector3d w_alpha = (imu_biases->w_hat2-imu_biases->w_hat)/dt;
        Eigen::Vector3d a_jerk = (imu_biases->a_hat2-imu_biases->a_hat)/dt;

        // y0 ================
        Eigen::Vector4d q_0 = Eigen::Matrix<double,4,1>{imu_biases->pose(0), imu_biases->pose(1), imu_biases->pose(2), imu_biases->pose(3)};
        Eigen::Vector3d p_0 = Eigen::Matrix<double,3,1>{imu_biases->pose(4), imu_biases->pose(5), imu_biases->pose(6)};
        Eigen::Vector3d v_0 = Eigen::Matrix<double,3,1>{imu_biases->pose(7), imu_biases->pose(8), imu_biases->pose(9)};

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
        //Eigen::Vector3d p_1 = p_0+0.5*k1_p;
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

        return {state_plus, imu_biases->imu_time};
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
