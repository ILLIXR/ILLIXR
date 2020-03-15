#include "common/component.hh"
#include "common/switchboard.hh"
#include "common/data_format.hh"
#include "data_loading.hh"
#include "common/data_format.hh"
#include <chrono>

using namespace ILLIXR;

const size_t TIME_SAFETY_FACTOR = 100;

template< class Rep, class Period >
static void reliable_sleep(const std::chrono::duration<Rep, Period>& sleep_duration) {
	auto start = std::chrono::high_resolution_clock::now();
	while (std::chrono::high_resolution_clock::now() - start < sleep_duration) {
		std::this_thread::sleep_for(sleep_duration / TIME_SAFETY_FACTOR);
	}
}

const std::string data_path = "data/";

class offline_imu_cam : public component {
public:
	offline_imu_cam(
		 std::unique_ptr<writer<imu_type>>&& imu0,
		 std::unique_ptr<writer<cam_type>>&& cam0,
		 std::unique_ptr<writer<cam_type>>&& cam1)
		: _m_sensor_data{load_data(data_path)}
		, _m_imu0{std::move(imu0)}
		, _m_cam0{std::move(cam0)}
		, _m_cam1{std::move(cam1)}
		, _m_sensor_data_it{_m_sensor_data.cbegin()}
	{
		last_time = _m_sensor_data_it->first;
	}

	virtual void _p_compute_one_iteration() override {
		ullong target_ts = _m_sensor_data_it->first;

		reliable_sleep(std::chrono::nanoseconds{target_ts});

		time_type ts = std::chrono::system_clock::now();

		const sensor_types& sensor_datum = _m_sensor_data_it->second;

		if (sensor_datum.imu0) {
			_m_imu0->put(new imu_type{
				ts,
				sensor_datum.imu0.value().angular_v,
				sensor_datum.imu0.value().linear_a,
			});
		}
		if (sensor_datum.cam0) {
			_m_cam0->put(new cam_type{
				ts,
				sensor_datum.cam0.value().load(),
				0,
			});
		}
		if (sensor_datum.cam1) {
			_m_cam0->put(new cam_type{
				ts,
				sensor_datum.cam0.value().load(),
				1,
			});
		}

		++_m_sensor_data_it;
	}

	virtual ~offline_imu_cam() override { }

private:
	const std::map<ullong, sensor_types> _m_sensor_data;
	std::map<ullong, sensor_types>::const_iterator _m_sensor_data_it;
	std::unique_ptr<writer<imu_type>> _m_imu0;
	std::unique_ptr<writer<cam_type>> _m_cam0;
	std::unique_ptr<writer<cam_type>> _m_cam1;
	ullong last_time;
};

extern "C" component* create_component(switchboard* sb) {
	auto imu0_ev = sb->publish<imu_type>("imu0");
	auto cam0_ev = sb->publish<cam_type>("cam0");
	auto cam1_ev = sb->publish<cam_type>("cam1");
	return new offline_imu_cam {std::move(imu0_ev), std::move(cam0_ev), std::move(cam1_ev)};
}
