#include "common/switchboard.hpp"
#include "common/data_format.hpp"
#include "data_loading.hpp"
#include "common/data_format.hpp"
#include "common/threadloop.hpp"
#include "common/realtime_clock.hpp"

using namespace ILLIXR;

class offline_imu : public ILLIXR::threadloop {
public:
	offline_imu(std::string name_, phonebook* pb_)
		: threadloop{name_, pb_, false}
		, _m_sensor_data{load_data()}
		, _m_sensor_data_it{_m_sensor_data.cbegin()}
		, _m_sb{pb->lookup_impl<switchboard>()}
		, _m_imu_cam{_m_sb->get_writer<imu_type>("imu")}
		, dataset_first_time{_m_sensor_data_it->first}
		, dataset_now{0}
		, imu_cam_log{record_logger_}
		, _m_rtc{pb->lookup_impl<realtime_clock>()}
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
				- _m_rtc->time_since_start()
			);

			return skip_option::run;

		} else {
			return skip_option::stop;
		}
	}

	virtual void _p_one_iteration() override {
		assert(_m_sensor_data_it != _m_sensor_data.end());
		//std::cerr << " IMU time: " << std::chrono::time_point<std::chrono::nanoseconds>(std::chrono::nanoseconds{dataset_now}).time_since_epoch().count() << std::endl;
		time_point real_now = _m_rtc->get_start() + std::chrono::nanoseconds{dataset_now - dataset_first_time};
		CPU_TIMER_TIME_EVENT_INFO(false, false, "entry", cpu_timer::make_type_eraser<FrameInfo>(std::to_string(id), "imu", 0, real_now));
		const sensor_types& sensor_datum = _m_sensor_data_it->second;

		_m_log << std::chrono::nanoseconds{std::chrono::steady_clock::now().time_since_epoch()}.count() << '\n';

		struct sched_param param;
		int rc = sched_getparam(get_tid(), &param);
		if (rc != 0) {
			abort();
		}
		if (param.sched_priority != 3) {
			std::cerr << "My priority isn't three." << std::endl;
			abort();
		}

		_m_imu_cam.put(new (_m_imu_cam.allocate()) imu_type{
			real_now,
			(sensor_datum.imu0.angular_v).cast<float>(),
			(sensor_datum.imu0.linear_a).cast<float>(),
			dataset_now,
		});
		++_m_sensor_data_it;
	}

private:
	const std::map<ullong, sensor_types> _m_sensor_data;
	std::map<ullong, sensor_types>::const_iterator _m_sensor_data_it;
	const std::shared_ptr<switchboard> _m_sb;
	switchboard::writer<imu_type> _m_imu_cam;

	// Timestamp of the first IMU value from the dataset
	ullong dataset_first_time;
	// Current IMU timestamp
	ullong dataset_now;

	record_coalescer imu_cam_log;

	std::shared_ptr<realtime_clock> _m_rtc;

	std::ofstream _m_log;
};

PLUGIN_MAIN(offline_imu)
