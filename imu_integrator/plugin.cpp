#include <chrono>
#include <iomanip>
#include <thread>
#include <eigen3/Eigen/Dense>

#include "../open_vins/ov_msckf/src/core/VioManager.h"
#include "../open_vins/ov_msckf/src/state/State.h"

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
		, _m_in{sb->subscribe_latest<imu_integrator_input>("imu_integrator_in")}
		, _m_ov_estimator{sb->subscribe_latest<ov_estimator>("ov_estimator")}
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
		auto in = _m_in->get_latest_ro();
		auto ov_estimator = _m_ov_estimator->get_latest_ro();

		// Do the IMU Integration
		if (_m_ov_estimator) {
			Eigen::Matrix<double,13,1> state_plus = Eigen::Matrix<double,13,1>::Zero();
			imu_raw_type *imu_raw_data = new imu_raw_type {
				Eigen::Matrix<double, 3, 1>::Zero(), 
				Eigen::Matrix<double, 3, 1>::Zero(), 
				Eigen::Matrix<double, 3, 1>::Zero(), 
				Eigen::Matrix<double, 3, 1>::Zero(),
				Eigen::Matrix<double, 13, 1>::Zero(),
				// Record the timestamp (in ILLIXR time) associated with this imu sample.
				// Used for MTP calculations.
				in->time
			};

        	ov_estimator->get_propagator()->fast_state_propagate(state, timestamp_in_seconds, state_plus, imu_raw_data);
			_m_imu_raw->put(imu_raw_data);
		}
	}

private:
	const std::shared_ptr<switchboard> sb;
	std::unique_ptr<reader_latest<imu_integrator_input>> _m_in;
	std::unique_ptr<reader_latest<ov_estimator>> _m_ov_estimator;
	std::unique_ptr<writer<imu_raw_type>> _m_imu_raw;
	long long _seq_expect, _stat_processed, _stat_missed;