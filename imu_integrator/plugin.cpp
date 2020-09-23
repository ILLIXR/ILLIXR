#include <chrono>
#include <iomanip>
#include <thread>
#include <eigen3/Eigen/Dense>

#include "../open_vins/ov_msckf/src/state/Propagator.h"
#include "../open_vins/ov_core/src/types/IMU.h"

#include "common/plugin.hpp"
#include "common/switchboard.hpp"
#include "common/data_format.hpp"
#include "common/threadloop.hpp"

using namespace ILLIXR;

class imu_integrator : public threadloop {
public:
	imu_integrator(std::string name_, phonebook* pb_)
		: threadloop{name_, pb_}
		, sb{pb->lookup_impl<switchboard>()}
		, _m_imu_cam{sb->subscribe_latest<imu_cam_type>("imu_cam")}
		, _m_in{sb->subscribe_latest<imu_integrator_seq>("imu_integrator_seq")}
		, _m_imu_integrator_input{sb->subscribe_latest<imu_integrator_input>("imu_integrator_input")}
		, _m_imu_raw{sb->publish<imu_raw_type>("imu_raw")}
		, _seq_expect(1)
	{}

	virtual skip_option _p_should_skip() override {
		auto in = _m_in->get_latest_ro();
		if (!in || in->seq == _seq_expect-1) {
			// No new data, sleep
			return skip_option::skip_and_yield;
		} else {
			if (in->seq != _seq_expect) {
				_stat_missed = in->seq - _seq_expect;
			} else {
				_stat_missed = 0;
			}
			_stat_processed++;
			_seq_expect = in->seq+1;
			return skip_option::run;
		}
	}

	void _p_one_iteration() override {
		const imu_cam_type *datum = _m_imu_cam->get_latest_ro();
		double timestamp_in_seconds = (double(datum->dataset_time) / NANO_SEC);

		ov_msckf::Propagator::IMUDATA data;
        data.timestamp = timestamp_in_seconds;
        data.wm = (datum->angular_v).cast<double>();
        data.am = (datum->linear_a).cast<double>();
		_imu_vec.emplace_back(data);

		clean_imu_vec(timestamp_in_seconds);
        propogate_imu_values(timestamp_in_seconds);
	}

private:
	const std::shared_ptr<switchboard> sb;

	// IMU Data, Sequence Flag, and State Vars Needed
	std::unique_ptr<reader_latest<imu_cam_type>> _m_imu_cam;
	std::unique_ptr<reader_latest<imu_integrator_seq>> _m_in;
	std::unique_ptr<reader_latest<imu_integrator_input>> _m_imu_integrator_input;

	// IMU Biases
	std::unique_ptr<writer<imu_raw_type>> _m_imu_raw;
	std::vector<ov_msckf::Propagator::IMUDATA> _imu_vec;
	double last_imu_offset;
	bool has_last_offset = false;
	long long _seq_expect, _stat_processed, _stat_missed;

	int counter = 0;
	int cam_count = 0;
	int total_imu = 0;
	double last_cam_time = 0;

	// Open_VINS cleans IMU values older than 20 seconds, we clean values older than 5 seconds
	void clean_imu_vec(double timestamp) {
		auto it0 = _imu_vec.begin();
        while (it0 != _imu_vec.end()) {
            if (timestamp-(*it0).timestamp > 5) {
                it0 = _imu_vec.erase(it0);
            } else {
                it0++;
            }
         }
	}

	// Timestamp we are propogating the biases to (new IMU reading time)
	void propogate_imu_values(double timestamp) {
		const imu_integrator_input *input_values = _m_imu_integrator_input->get_latest_ro();
		if (input_values == NULL || !input_values->slam_ready) {
			return;
		}

		if (!has_last_offset) {
			last_imu_offset = input_values->t_offset;
			has_last_offset = true;
		}

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

		vector<ov_msckf::Propagator::IMUDATA> prop_data = select_imu_readings(_imu_vec, time0, time1);
		ov_type::IMU *temp_imu = new ov_type::IMU();
		temp_imu->set_value(input_values->imu_value);
		temp_imu->set_fej(input_values->imu_fej);

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
				w_hat = prop_data.at(i).wm - temp_imu->bias_g();
				a_hat = prop_data.at(i).am - temp_imu->bias_a();
				w_hat2 = prop_data.at(i+1).wm - temp_imu->bias_g();
				a_hat2 = prop_data.at(i+1).am - temp_imu->bias_a();

				// Compute the new state mean value
				Eigen::Vector4d new_q;
				Eigen::Vector3d new_v, new_p;
				predict_mean_rk4(temp_imu->quat(), temp_imu->pos(), temp_imu->vel(), input_values->gravity,
								dt, w_hat, a_hat, w_hat2, a_hat2, new_q, new_v, new_p);

				//Now replace imu estimate and fej with propagated values
				Eigen::Matrix<double,16,1> imu_x = temp_imu->value();
				imu_x.block(0,0,4,1) = new_q;
				imu_x.block(4,0,3,1) = new_p;
				imu_x.block(7,0,3,1) = new_v;
				temp_imu->set_value(imu_x);
				temp_imu->set_fej(imu_x);
			}
		}

		Eigen::Matrix<double,13,1> state_plus = Eigen::Matrix<double,13,1>::Zero();
		state_plus.block(0,0,4,1) = temp_imu->quat();
		state_plus.block(4,0,3,1) = temp_imu->pos();
		state_plus.block(7,0,3,1) = temp_imu->vel();

		if (prop_data.size() > 1) state_plus.block(10,0,3,1) = prop_data.at(prop_data.size()-2).wm - temp_imu->bias_g();
		else if (!prop_data.empty()) state_plus.block(10,0,3,1) = prop_data.at(prop_data.size()-1).wm - temp_imu->bias_g();

		_m_imu_raw->put(new imu_raw_type{
			w_hat,
			a_hat,
			w_hat2,
			a_hat2,
			state_plus,
		});
    }

	std::vector<ov_msckf::Propagator::IMUDATA> select_imu_readings(const std::vector<ov_msckf::Propagator::IMUDATA>& imu_data, double time0, double time1) {

		// Our vector imu readings
		std::vector<ov_msckf::Propagator::Propagator::IMUDATA> prop_data;

		// Ensure we have some measurements in the first place!
		if(imu_data.empty()) {
			printf(YELLOW "Propagator::select_imu_readings(): No IMU measurements. IMU-CAMERA are likely messed up!!!\n" RESET);
			return prop_data;
		}

		// Loop through and find all the needed measurements to propagate with
		// Note we split measurements based on the given state time, and the update timestamp
		for(size_t i=0; i<imu_data.size()-1; i++) {

			// START OF THE INTEGRATION PERIOD
			// If the next timestamp is greater then our current state time
			// And the current is not greater then it yet...
			// Then we should "split" our current IMU measurement
			if(imu_data.at(i+1).timestamp > time0 && imu_data.at(i).timestamp < time0) {
				ov_msckf::Propagator::IMUDATA data = interpolate_data(imu_data.at(i),imu_data.at(i+1), time0);
				prop_data.push_back(data);
				//printf("propagation #%d = CASE 1 = %.3f => %.3f\n", (int)i,data.timestamp-prop_data.at(0).timestamp,time0-prop_data.at(0).timestamp);
				continue;
			}

			// MIDDLE OF INTEGRATION PERIOD
			// If our imu measurement is right in the middle of our propagation period
			// Then we should just append the whole measurement time to our propagation vector
			if(imu_data.at(i).timestamp >= time0 && imu_data.at(i+1).timestamp <= time1) {
				prop_data.push_back(imu_data.at(i));
				//printf("propagation #%d = CASE 2 = %.3f\n",(int)i,imu_data.at(i).timestamp-prop_data.at(0).timestamp);
				continue;
			}

			// END OF THE INTEGRATION PERIOD
			// If the current timestamp is greater then our update time
			// We should just "split" the NEXT IMU measurement to the update time,
			// NOTE: we add the current time, and then the time at the end of the interval (so we can get a dt)
			// NOTE: we also break out of this loop, as this is the last IMU measurement we need!
			if(imu_data.at(i+1).timestamp > time1) {
				// If we have a very low frequency IMU then, we could have only recorded the first integration (i.e. case 1) and nothing else
				// In this case, both the current IMU measurement and the next is greater than the desired intepolation, thus we should just cut the current at the desired time
				// Else, we have hit CASE2 and this IMU measurement is not past the desired propagation time, thus add the whole IMU reading
				if(imu_data.at(i).timestamp > time1) {
					ov_msckf::Propagator::IMUDATA data = interpolate_data(imu_data.at(i-1), imu_data.at(i), time1);
					prop_data.push_back(data);
					//printf("propagation #%d = CASE 3.1 = %.3f => %.3f\n", (int)i,imu_data.at(i).timestamp-prop_data.at(0).timestamp,imu_data.at(i).timestamp-time0);
				} else {
					prop_data.push_back(imu_data.at(i));
					//printf("propagation #%d = CASE 3.2 = %.3f => %.3f\n", (int)i,imu_data.at(i).timestamp-prop_data.at(0).timestamp,imu_data.at(i).timestamp-time0);
				}
				// If the added IMU message doesn't end exactly at the camera time
				// Then we need to add another one that is right at the ending time
				if(prop_data.at(prop_data.size()-1).timestamp != time1) {
					ov_msckf::Propagator::IMUDATA data = interpolate_data(imu_data.at(i), imu_data.at(i+1), time1);
					prop_data.push_back(data);
					//printf("propagation #%d = CASE 3.3 = %.3f => %.3f\n", (int)i,data.timestamp-prop_data.at(0).timestamp,data.timestamp-time0);
				}
				break;
			}

		}

		// Check that we have at least one measurement to propagate with
		if(prop_data.empty()) {
			printf(YELLOW "Propagator::select_imu_readings(): No IMU measurements to propagate with (%d of 2). IMU-CAMERA are likely messed up!!!\n" RESET, (int)prop_data.size());
			return prop_data;
		}

		// If we did not reach the whole integration period (i.e., the last inertial measurement we have is smaller then the time we want to reach)
		// Then we should just "stretch" the last measurement to be the whole period (case 3 in the above loop)
		//if(time1-imu_data.at(imu_data.size()-1).timestamp > 1e-3) {
		//    printf(YELLOW "Propagator::select_imu_readings(): Missing inertial measurements to propagate with (%.6f sec missing). IMU-CAMERA are likely messed up!!!\n" RESET, (time1-imu_data.at(imu_data.size()-1).timestamp));
		//    return prop_data;
		//}

		// Loop through and ensure we do not have an zero dt values
		// This would cause the noise covariance to be Infinity
		for (size_t i=0; i < prop_data.size()-1; i++) {
			if (std::abs(prop_data.at(i+1).timestamp-prop_data.at(i).timestamp) < 1e-12) {
				printf(YELLOW "Propagator::select_imu_readings(): Zero DT between IMU reading %d and %d, removing it!\n" RESET, (int)i, (int)(i+1));
				prop_data.erase(prop_data.begin()+i);
				i--;
			}
		}

		// Check that we have at least one measurement to propagate with
		if(prop_data.size() < 2) {
			printf(YELLOW "Propagator::select_imu_readings(): No IMU measurements to propagate with (%d of 2). IMU-CAMERA are likely messed up!!!\n" RESET, (int)prop_data.size());
			return prop_data;
		}

		// Success :D
		return prop_data;
	}

	static ov_msckf::Propagator::IMUDATA interpolate_data(const ov_msckf::Propagator::IMUDATA imu_1, const ov_msckf::Propagator::IMUDATA imu_2, double timestamp) {
		// time-distance lambda
		double lambda = (timestamp - imu_1.timestamp) / (imu_2.timestamp - imu_1.timestamp);
		//cout << "lambda - " << lambda << endl;
		// interpolate between the two times
		ov_msckf::Propagator::IMUDATA data;
		data.timestamp = timestamp;
		data.am = (1 - lambda) * imu_1.am + lambda * imu_2.am;
		data.wm = (1 - lambda) * imu_1.wm + lambda * imu_2.wm;
		return data;
	}

	void predict_mean_rk4(Eigen::Vector4d quat, Eigen::Vector3d pos, Eigen::Vector3d vel, 
                                  Eigen::Matrix<double, 3, 1> _gravity, double dt,
                                  const Eigen::Vector3d &w_hat1, const Eigen::Vector3d &a_hat1,
                                  const Eigen::Vector3d &w_hat2, const Eigen::Vector3d &a_hat2,
                                  Eigen::Vector4d &new_q, Eigen::Vector3d &new_v, Eigen::Vector3d &new_p) {

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
		Eigen::Vector3d v0_dot = R_Gto0.transpose()*a_hat-_gravity;

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
		Eigen::Vector3d v1_dot = R_Gto1.transpose()*a_hat-_gravity;

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
		Eigen::Vector3d v2_dot = R_Gto2.transpose()*a_hat-_gravity;

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
		Eigen::Vector3d v3_dot = R_Gto3.transpose()*a_hat-_gravity;

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
