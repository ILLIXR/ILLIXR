#include "common/switchboard.hpp"
#include "common/data_format.hpp"
#include "data_loading.hpp"
#include "common/data_format.hpp"
#include "common/threadloop.hpp"

using namespace ILLIXR;

class offline_imu_cam : public ILLIXR::threadloop {
public:
	offline_imu_cam(std::string name_, phonebook* pb_)
		: threadloop{name_, pb_}
		, _m_sensor_data{load_data(std::string{std::getenv("ILLIXR_DATA")})}
		, _m_sensor_data_it{_m_sensor_data.cbegin()}
		, _m_sb{pb->lookup_impl<switchboard>()}
		, _m_imu_cam{_m_sb->publish<imu_cam_type>("imu_cam")}
		, dataset_first_time{_m_sensor_data_it->first}
	{ }

protected:
	virtual skip_option _p_should_skip() override {
		if (_m_sensor_data_it != _m_sensor_data.end()) {
			dataset_now = _m_sensor_data_it->first;
			reliable_sleep(std::chrono::nanoseconds{dataset_now - dataset_first_time} + real_first_time);
			real_now = real_first_time + std::chrono::nanoseconds{dataset_now - dataset_first_time};
			return skip_option::run;
		} else {
			return skip_option::stop;
		}
	}

	virtual void _p_one_iteration() override {
		assert(_m_sensor_data_it != _m_sensor_data.end());
		//std::cerr << " IMU time: " << std::chrono::time_point<std::chrono::nanoseconds>(std::chrono::nanoseconds{dataset_now}).time_since_epoch().count() << std::endl;

		const sensor_types& sensor_datum = _m_sensor_data_it->second;
		if (sensor_datum.imu0) {
			_m_imu_cam->put(new imu_cam_type{
				real_now,
				(sensor_datum.imu0.value().angular_v).cast<float>(),
				(sensor_datum.imu0.value().linear_a).cast<float>(),
				sensor_datum.cam0
					? std::make_optional<cv::Mat*>(sensor_datum.cam0.value().load().release())
					: std::nullopt,
				sensor_datum.cam1
					? std::make_optional<cv::Mat*>(sensor_datum.cam1.value().load().release())
					: std::nullopt,
				dataset_now,
			});
		}

		++_m_sensor_data_it;
	}

public:
	virtual void start() override {
		// this is not done in the constructor, because I want it to
		// be done at thread-launch time, not load-time.
		real_first_time = std::chrono::system_clock::now();
		threadloop::start();
	}

private:
	const std::map<ullong, sensor_types> _m_sensor_data;
	std::map<ullong, sensor_types>::const_iterator _m_sensor_data_it;
	const std::shared_ptr<switchboard> _m_sb;
	std::unique_ptr<writer<imu_cam_type>> _m_imu_cam;

	ullong dataset_first_time;
	time_type real_first_time;
	ullong dataset_now;
	time_type real_now;
};

PLUGIN_MAIN(offline_imu_cam)
