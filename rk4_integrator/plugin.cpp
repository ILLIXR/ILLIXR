// This entire IMU integrator has been ported almost as-is from the original OpenVINS integrator, which
// can be found here: https://github.com/rpng/open_vins/blob/master/ov_msckf/src/state/Propagator.cpp

#include <chrono>
#include <iomanip>
#include <thread>
#include <eigen3/Eigen/Dense>

#include "common/plugin.hpp"
#include "common/switchboard.hpp"
#include "common/data_format.hpp"
#include "common/plugin.hpp"

using namespace ILLIXR;

typedef struct {
	double timestamp;
	Eigen::Matrix<double, 3, 1> wm;
	Eigen::Matrix<double, 3, 1> am;
} imu_type;

#define IMU_SAMPLE_LIFETIME 5

class imu_integrator : public threadloop {
public:
	imu_integrator(std::string name_, phonebook* pb_)
		: threadloop{name_, pb_}
		, sb{pb->lookup_impl<switchboard>()}
		, _m_imu_cam{sb->subscribe_latest<imu_cam_type>("imu_cam")}
		, _m_in{sb->subscribe_latest<imu_integrator_seq>("imu_integrator_seq")}
		, _m_imu_integrator_input{sb->subscribe_latest<imu_integrator_input>("imu_integrator_input")}
		, _m_imu_raw{sb->publish<imu_raw_type>("imu_raw")}
	{}

class imu_integrator : public plugin {
public:
	imu_integrator(std::string name_, phonebook* pb_)
		: plugin{name_, pb_}
		, sb{pb->lookup_impl<switchboard>()}
		, _m_imu_cam{sb->get_reader<imu_cam_type>("imu_cam")}
		, _m_in{sb->get_reader<imu_integrator_seq>("imu_integrator_seq")}
		, _m_imu_integrator_input{sb->get_reader<imu_integrator_input>("imu_integrator_input")}
		, _m_imu_raw{sb->get_writer<imu_raw_type>("imu_raw")}
	{
		sb.schedule<imu_cam_type>(id, "imu_cam", [&](switchboard::ptr<const imu_cam_type> datum, size_t) {
			callback(datum);
		});
	}

	void callback(switchboard::ptr<const imu_cam_type> datum) {
		double timestamp_in_seconds = (double(datum->dataset_time) / NANO_SEC);

		imu_type data;
        data.timestamp = timestamp_in_seconds;
        data.wm = (datum->angular_v).cast<double>();
        data.am = (datum->linear_a).cast<double>();
		_imu_vec.emplace_back(data);

		clean_imu_vec(timestamp_in_seconds);
        propagate_imu_values(timestamp_in_seconds, datum->time);
	}

private:
	const std::shared_ptr<switchboard> sb;

	// IMU Data, Sequence Flag, and State Vars Needed
	switchboard::reader<imu_cam_type>> _m_imu_cam;
	switchboard::reader<imu_integrator_seq>> _m_in;
	switchboard::reader<imu_integrator_input> _m_imu_integrator_input;

	// IMU Biases
	switchboard::writer<imu_raw_type> _m_imu_raw;
	std::vector<imu_type> _imu_vec;
	double last_imu_offset;
	bool has_last_offset = false;

	[[maybe_unused]] int counter = 0;
	[[maybe_unused]] int cam_count = 0;
	[[maybe_unused]] int total_imu = 0;
	[[maybe_unused]] double last_cam_time = 0;

	// Clean IMU values older than IMU_SAMPLE_LIFETIME seconds
	void clean_imu_vec(double timestamp) {
		auto it0 = _imu_vec.begin();
        while (it0 != _imu_vec.end()) {
            if (timestamp-(*it0).timestamp > IMU_SAMPLE_LIFETIME) {
                it0 = _imu_vec.erase(it0);
            } else {
                it0++;
            }
         }
	}

	// Timestamp we are propagating the biases to (new IMU reading time)
	void propagate_imu_values(double timestamp, time_type real_time) {
        auto input_values = _m_imu_integrator_input.get_nullable();
        if (!input_values) {
            return;
        }

		if (!has_last_offset) {
			last_imu_offset = input_values->t_offset;
			has_last_offset = true;
		}

		Eigen::Matrix<double,4,1> curr_quat = Eigen::Matrix<double,4,1>{input_values->quat.x(), 
				input_values->quat.y(), input_values->quat.z(), input_values->quat.w()};
		Eigen::Matrix<double,3,1> curr_pos = input_values->position;
		Eigen::Matrix<double,3,1> curr_vel = input_values->velocity;

		// Uncomment this for some helpful prints
		// total_imu++;
		// if (input_values->last_cam_integration_time > last_cam_time) {
		// 	cam_count++;
		// 	last_cam_time = input_values->last_cam_integration_time;
		// 	std::cout << "Num IMUs recieved since last cam: " << counter << " Diff between new cam and latest IMU: " 
		// 			  << timestamp - last_cam_time << " Expected IMUs recieved VS Actual: " << cam_count*10 << ", " << total_imu << std::endl;
		// 	counter = 0;
		// }
		// counter++;

		// Get what our IMU-camera offset should be (t_imu = t_cam + calib_dt)
		double t_off_new = input_values->t_offset;

		// This is the last CAM time
		double time0 = input_values->last_cam_integration_time + last_imu_offset;
		double time1 = timestamp + t_off_new;

		std::vector<imu_type> prop_data = select_imu_readings(_imu_vec, time0, time1);
		Eigen::Matrix<double,3,1> w_hat;
		Eigen::Matrix<double,3,1> a_hat;
		Eigen::Matrix<double,3,1> w_hat2;
		Eigen::Matrix<double,3,1> a_hat2;

		// Loop through all IMU messages, and use them to move the state forward in time
		// This uses the zero'th order quat, and then constant acceleration discrete
		if (prop_data.size() > 1) {
			for(size_t i=0; i<prop_data.size()-1; i++) {

				// Time elapsed over interval
				double dt = prop_data.at(i+1).timestamp-prop_data.at(i).timestamp;

				// Corrected imu measurements
				w_hat = prop_data.at(i).wm - input_values->biasGyro;
				a_hat = prop_data.at(i).am - input_values->biasAcc;
				w_hat2 = prop_data.at(i+1).wm - input_values->biasGyro;
				a_hat2 = prop_data.at(i+1).am - input_values->biasAcc;

				// Compute the new state mean value
				Eigen::Vector4d new_quat;
				Eigen::Vector3d new_vel, new_pos;
				predict_mean_rk4(curr_quat, curr_pos, curr_vel, dt, w_hat, a_hat, w_hat2, a_hat2, new_quat, new_vel, new_pos);

				curr_quat = new_quat;
				curr_pos = new_pos;
				curr_vel = new_vel;
			}
		}

		_m_imu_raw->put(new imu_raw_type{
			.w_hat = w_hat,
			.a_hat = a_hat,
			.w_hat2 = w_hat2,
			.a_hat2 = a_hat2,
			.pos = curr_pos,
			.vel = curr_vel,
			.quat = Eigen::Quaterniond{curr_quat(3), curr_quat(0), curr_quat(1), curr_quat(2)},
			.imu_time = real_time,
		});
    }

	// Select IMU readings based on timestamp similar to how OpenVINS selects IMU values to propagate
	std::vector<imu_type> select_imu_readings(const std::vector<imu_type>& imu_data, double time_begin, double time_end) {
		std::vector<imu_type> prop_data;
		if (imu_data.size() < 2) {
			return prop_data;
		}

		for (unsigned i = 0; i < imu_data.size()-1; i++) {

			// If time_begin comes inbetween two IMUs (A and B), interpolate A forward to time_begin
			if (imu_data.at(i+1).timestamp > time_begin && imu_data.at(i).timestamp < time_begin) {
				imu_type data = interpolate_imu(imu_data.at(i), imu_data.at(i+1), time_begin);
				prop_data.push_back(data);
				continue;
			}

			// IMU is within time_begin and time_end
			if (imu_data.at(i).timestamp >= time_begin && imu_data.at(i+1).timestamp <= time_end) {
				prop_data.push_back(imu_data.at(i));
				continue;
			}

			// IMU is past time_end
			if (imu_data.at(i+1).timestamp > time_end) {
				imu_type data = interpolate_imu(imu_data.at(i), imu_data.at(i+1), time_end);
				prop_data.push_back(data);
				break;
			}
		}

		// Loop through and ensure we do not have an zero dt values
		// This would cause the noise covariance to be Infinity
		for (size_t i = 0; i < prop_data.size()-1; i++) {
			if (std::abs(prop_data.at(i+1).timestamp-prop_data.at(i).timestamp) < 1e-12) {
				prop_data.erase(prop_data.begin()+i);
				i--;
			}
		}

		return prop_data;
	}

	// For when an integration time ever falls inbetween two imu measurements (modeled after OpenVINS)
	static imu_type interpolate_imu(const imu_type imu_1, imu_type imu_2, double timestamp) {
		imu_type data;
		data.timestamp = timestamp;

		double lambda = (timestamp - imu_1.timestamp) / (imu_2.timestamp - imu_1.timestamp);
		data.am = (1 - lambda) * imu_1.am + lambda * imu_2.am;
		data.wm = (1 - lambda) * imu_1.wm + lambda * imu_2.wm;

		return data;
	}

	void predict_mean_rk4(Eigen::Vector4d quat, Eigen::Vector3d pos, Eigen::Vector3d vel, double dt,
                                  const Eigen::Vector3d &w_hat1, const Eigen::Vector3d &a_hat1,
                                  const Eigen::Vector3d &w_hat2, const Eigen::Vector3d &a_hat2,
                                  Eigen::Vector4d &new_q, Eigen::Vector3d &new_v, Eigen::Vector3d &new_p) {
									  
		Eigen::Matrix<double,3,1> gravity_vec = Eigen::Matrix<double,3,1>(0.0, 0.0, 9.81);

		// Pre-compute things
		Eigen::Vector3d w_hat = w_hat1;
		Eigen::Vector3d a_hat = a_hat1;
		Eigen::Vector3d w_alpha = (w_hat2-w_hat1)/dt;
		Eigen::Vector3d a_jerk = (a_hat2-a_hat1)/dt;

		// y0 ================
		Eigen::Vector4d q_0 = quat;
		Eigen::Vector3d p_0 = pos;
		Eigen::Vector3d v_0 = vel;

		// k1 ================
		Eigen::Vector4d dq_0 = {0,0,0,1};
		Eigen::Vector4d q0_dot = 0.5*Omega(w_hat)*dq_0;
		Eigen::Vector3d p0_dot = v_0;
		Eigen::Matrix3d R_Gto0 = quat_2_Rot(quat_multiply(dq_0,q_0));
		Eigen::Vector3d v0_dot = R_Gto0.transpose()*a_hat-gravity_vec;

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
		Eigen::Vector3d v1_dot = R_Gto1.transpose()*a_hat-gravity_vec;

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
		Eigen::Vector3d v2_dot = R_Gto2.transpose()*a_hat-gravity_vec;

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
		Eigen::Vector3d v3_dot = R_Gto3.transpose()*a_hat-gravity_vec;

		Eigen::Vector4d k4_q = q3_dot*dt;
		Eigen::Vector3d k4_p = p3_dot*dt;
		Eigen::Vector3d k4_v = v3_dot*dt;

		// y+dt ================
		Eigen::Vector4d dq = quatnorm(dq_0+(1.0/6.0)*k1_q+(1.0/3.0)*k2_q+(1.0/3.0)*k3_q+(1.0/6.0)*k4_q);
		new_q = quat_multiply(dq, q_0);
		new_p = p_0+(1.0/6.0)*k1_p+(1.0/3.0)*k2_p+(1.0/3.0)*k3_p+(1.0/6.0)*k4_p;
		new_v = v_0+(1.0/6.0)*k1_v+(1.0/3.0)*k2_v+(1.0/3.0)*k3_v+(1.0/6.0)*k4_v;
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

PLUGIN_MAIN(imu_integrator)
