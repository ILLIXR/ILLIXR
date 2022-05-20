#include "common/switchboard.hpp"
#include "common/data_format.hpp"
#include "data_loading.hpp"
#include "common/data_format.hpp"
#include "common/threadloop.hpp"
#include "common/relative_clock.hpp"
#include "common/managed_thread.hpp"

using namespace ILLIXR;

class offline_imu : public ILLIXR::threadloop {
public:
	offline_imu(std::string name_, phonebook* pb_)
		: threadloop{name_, pb_}
		, _m_sensor_data{load_data()}
		, _m_sensor_data_it{_m_sensor_data.cbegin()}
		, _m_sb{pb->lookup_impl<switchboard>()}
		, _m_imu{_m_sb->get_writer<imu_type>("imu")}
		, dataset_first_time{_m_sensor_data_it->first}
		, dataset_now{0}
		, imu_cam_log{record_logger_}
		, _m_rtc{pb->lookup_impl<RelativeClock>()}
		, _m_log{"metrics/offline_imu.csv"}
	{
		_m_log << "offline_imu_put\n";
	}

protected:
	virtual skip_option _p_should_skip() override {
		if (_m_sensor_data_it != _m_sensor_data.end()) {
			assert(dataset_now < _m_sensor_data_it->first);
			dataset_now = _m_sensor_data_it->first;
			// Sleep for the difference between the current IMU vs 1st IMU and current UNIX time vs UNIX time the component was init
			std::this_thread::sleep_for(
				std::chrono::nanoseconds{dataset_now - dataset_first_time}
				- _m_rtc->now().time_since_epoch()
			);

			return skip_option::run;

		} else {
			return skip_option::stop;
		}
	}

	virtual void _p_one_iteration() override {
		assert(_m_sensor_data_it != _m_sensor_data.end());
		time_point real_now(std::chrono::duration<long, std::nano>{dataset_now - dataset_first_time});
		const sensor_types& sensor_datum = _m_sensor_data_it->second;

		_m_log << std::chrono::nanoseconds{std::chrono::steady_clock::now().time_since_epoch()}.count() << '\n';

		_m_imu.put(_m_imu.allocate<imu_type>(
			imu_type {
				real_now,
				(sensor_datum.imu0.angular_v).cast<float>(),
				(sensor_datum.imu0.linear_a).cast<float>()
			}
		));
		++_m_sensor_data_it;
	}

private:
	const std::map<ullong, sensor_types> _m_sensor_data;
	std::map<ullong, sensor_types>::const_iterator _m_sensor_data_it;
	const std::shared_ptr<switchboard> _m_sb;
	switchboard::writer<imu_type> _m_imu;

	// Timestamp of the first IMU value from the dataset
	ullong dataset_first_time;
	// Current IMU timestamp
	ullong dataset_now;

	record_coalescer imu_cam_log;

	std::shared_ptr<RelativeClock> _m_rtc;

	std::ofstream _m_log;
};

PLUGIN_MAIN(offline_imu)
