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
		 std::unique_ptr<writer<cam_type>>&& cams)
		: _m_sensor_data{load_data(data_path)}
		, _m_imu0{std::move(imu0)}
		, _m_cams{std::move(cams)}
		, _m_sensor_data_it{_m_sensor_data.cbegin()}
	{
		first_time = last_time = _m_sensor_data_it->first;
		begin_time = std::chrono::system_clock::now();
		assert(_m_imu0);
	}

	virtual void _p_compute_one_iteration() override {
		assert(_m_imu0);
		ullong target_ts = _m_sensor_data_it->first;

		reliable_sleep(std::chrono::nanoseconds{target_ts - last_time});

		time_type ts = begin_time + std::chrono::nanoseconds{target_ts - first_time};
		std::cout << "Now time: " << ts.time_since_epoch().count() << " IMU time: " << std::chrono::time_point<std::chrono::nanoseconds>(std::chrono::nanoseconds{target_ts}).time_since_epoch().count() << std::endl;

		const sensor_types& sensor_datum = _m_sensor_data_it->second;

		if (sensor_datum.imu0) {
			_m_imu0->put(new imu_type{
				ts,
				(sensor_datum.imu0.value().angular_v).cast<float>(),
				(sensor_datum.imu0.value().linear_a).cast<float>(),
			});
		}
		if (sensor_datum.cam0) {
			assert(sensor_datum.cam1);
			_m_cams->put(new cam_type{
				ts,
				sensor_datum.cam0.value().load(),
				sensor_datum.cam1.value().load(),
			});
		}

		last_time = target_ts;
		++_m_sensor_data_it;
	}

	virtual ~offline_imu_cam() override { }

private:
	const std::map<ullong, sensor_types> _m_sensor_data;
	std::map<ullong, sensor_types>::const_iterator _m_sensor_data_it;
	std::unique_ptr<writer<imu_type>> _m_imu0;
	std::unique_ptr<writer<cam_type>> _m_cams;
	ullong last_time;
	ullong first_time;
	time_type begin_time;
};

extern "C" component* create_component(switchboard* sb) {
	auto imu0_ev = sb->publish<imu_type>("imu0");
	imu0_ev->put(new imu_type{std::chrono::system_clock::now(), Eigen::Vector3f{0, 0, 0}, Eigen::Vector3f{0, 0, 0}});
	assert(imu0_ev);
	
	auto cams_ev = sb->publish<cam_type>("cams");
	auto f = new offline_imu_cam {std::move(imu0_ev), std::move(cams_ev)};
	return f;
}
