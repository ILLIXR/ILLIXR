#include <chrono>
#include <iomanip>
#include <thread>
#include <eigen3/Eigen/Dense>

#include "common/plugin.hpp"
#include "common/switchboard.hpp"
#include "common/data_format.hpp"
#include "common/threadloop.hpp"

#include <gtsam/base/Matrix.h>
#include <gtsam/base/Vector.h>
#include <gtsam/navigation/AHRSFactor.h>
#include <gtsam/navigation/CombinedImuFactor.h>  // Used if IMU combined is off.
#include <gtsam/navigation/ImuBias.h>
#include <gtsam/navigation/ImuFactor.h>

using ImuBias = gtsam::imuBias::ConstantBias;
using namespace ILLIXR;

typedef struct {
	double timestamp;
	Eigen::Matrix<double, 3, 1> wm;
	Eigen::Matrix<double, 3, 1> am;
} imu_type;

class imu_integrator : public threadloop {
public:
	imu_integrator(std::string name_, phonebook* pb_)
		: threadloop{name_, pb_}
		, sb{pb->lookup_impl<switchboard>()}
		, _m_imu_cam{sb->subscribe_latest<imu_cam_type>("imu_cam")}
		, _m_in{sb->subscribe_latest<imu_integrator_seq>("imu_integrator_seq")}
		, _m_imu_integrator_input{sb->subscribe_latest<imu_integrator_input2>("imu_integrator_input")}
		, _m_imu_raw{sb->publish<imu_raw_type>("imu_raw")}
		, _seq_expect(1)
	{}

	virtual skip_option _p_should_skip() override {
		auto in = _m_in->get_latest_ro();
		if (!in || in->seq == _seq_expect-1) {
			// No new data, sleep to keep CPU utilization low
			std::this_thread::sleep_for(std::chrono::milliseconds{1});
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
	std::unique_ptr<reader_latest<imu_cam_type>> _m_imu_cam;
	std::unique_ptr<reader_latest<imu_integrator_seq>> _m_in;
	std::unique_ptr<reader_latest<imu_integrator_input2>> _m_imu_integrator_input;

	// Write IMU Biases for PP
	std::unique_ptr<writer<imu_raw_type>> _m_imu_raw;

	double last_cam_time = 0;
	bool slam_ready = false;
	std::vector<imu_type> _imu_vec;
	gtsam::PreintegratedCombinedMeasurements pim_;

	int counter = 0;
	int cam_count = 0;
	int total_imu = 0;
	long long _seq_expect, _stat_processed, _stat_missed;

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

	// Timestamp we are propagating the biases to (new IMU reading time)
	void propagate_imu_values(double timestamp, time_type real_time) {
		const imu_integrator_input2 *input_values = _m_imu_integrator_input->get_latest_ro();
		if (input_values == NULL || !input_values->slam_ready) {
			return;
		}

		ImuBias imu_bias = ImuBias(input_values->biasAcc, input_values->biasGyro);
		// if (!input_values->slam_ready) {
		// 	slam_ready = true;
		// 	// auto params = gtsam::PreintegratedCombinedMeasurements::Params(gtsam::Vector3::Zero());
		// 	auto params = gtsam::PreintegratedCombinedMeasurements::Params();

		// 	params.setGyroscopeCovariance(std::pow(input_values->gyro_noise, 2.0) * Eigen::Matrix3d::Identity());
  		// 	params.setAccelerometerCovariance(std::pow(input_values->acc_noise, 2.0) * Eigen::Matrix3d::Identity());
  		// 	params.setIntegrationCovariance(std::pow(input_values->imu_integration_sigma, 2.0) * Eigen::Matrix3d::Identity());
		// 	params.biasAccCovariance = std::pow(input_values->acc_walk, 2.0) * Eigen::Matrix3d::Identity();
		// 	params.biasOmegaCovariance = std::pow(input_values->gyro_walk, 2.0) * Eigen::Matrix3d::Identity();

		// 	pim_ = gtsam::PreintegratedCombinedMeasurements(params, imu_bias);
		// }

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

		if (input_values->last_cam_integration_time > last_cam_time) {
			pim_.resetIntegrationAndSetBias(imu_bias);
		}

		// This is the last CAM time
		double time_begin = input_values->last_cam_integration_time;
		double time_end = timestamp;

		ImuBias prev_bias = pim_.biasHat();
		ImuBias bias = pim_.biasHat();
		std::vector<imu_type> prop_data = select_imu_readings(_imu_vec, time_begin, time_end);
		for (int i = 0; i < prop_data.size()-1; i++) {
			const gtsam::Vector3& measured_acc = prop_data.at(i).am;
			const gtsam::Vector3& measured_omega = prop_data.at(i).wm;
			const double& delta_t = prop_data.at(i+1).timestamp - prop_data.at(i).timestamp;
			pim_.integrateMeasurement(measured_acc, measured_omega, delta_t);

			prev_bias = bias;
			bias = pim_.biasHat();
		}
		
		_m_imu_raw->put(new imu_raw_type{
			prev_bias.gyroscope(),
			prev_bias.accelerometer(),
			bias.gyroscope(),
			bias.accelerometer(),
			pim_.deltaPij(), // Position
			pim_.deltaVij(), // Velocity
			pim_.deltaRij().toQuaternion(), // Eigen Quat
			real_time
		});
    }

	std::vector<imu_type> select_imu_readings(const std::vector<imu_type>& imu_data, double time_begin, double time_end) {
		std::vector<imu_type> prop_data;
		if (imu_data.empty()) {
			return prop_data;
		}

		for (size_t i = 0; i < imu_data.size()-1; i++) {

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
			if(imu_data.at(i+1).timestamp > time_end) {
				if(imu_data.at(i).timestamp > time_end) {
					imu_type data = interpolate_imu(imu_data.at(i-1), imu_data.at(i), time_end);
					prop_data.push_back(data);
				} else {
					prop_data.push_back(imu_data.at(i));
				}

				if(prop_data.at(prop_data.size()-1).timestamp != time_end) {
					imu_type data = interpolate_imu(imu_data.at(i), imu_data.at(i+1), time_end);
					prop_data.push_back(data);
				}
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

	// If an integration time ever falls inbetween two imu measurements, interpolate to it
	static imu_type interpolate_imu(const imu_type imu_1, imu_type imu_2, double timestamp) {
		imu_type data;
		data.timestamp = timestamp;

		double lambda = (timestamp - imu_1.timestamp) / (imu_2.timestamp - imu_1.timestamp);
		data.am = (1 - lambda) * imu_1.am + lambda * imu_2.am;
		data.wm = (1 - lambda) * imu_1.wm + lambda * imu_2.wm;

		return data;
	}
};

PLUGIN_MAIN(imu_integrator)
