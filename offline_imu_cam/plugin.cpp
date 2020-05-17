#include "common/switchboard.hpp"
#include "common/data_format.hpp"
#include "data_loading.hpp"
#include "common/data_format.hpp"
#include "common/threadloop.hpp"

using namespace ILLIXR;

const std::string data_path = "data1/";

class offline_imu_cam : public ILLIXR::threadloop {
public:
	offline_imu_cam(phonebook* pb)
		: _m_sensor_data{load_data(data_path)}
		, _m_sb{pb->lookup_impl<switchboard>()}
		, _m_imu_cam{_m_sb->publish<imu_cam_type>("imu_cam")}
		, _m_sensor_data_it{_m_sensor_data.cbegin()}
	{
		dataset_first_time = _m_sensor_data_it->first;
		// last_time = _m_sensor_data_it->first;
		
		real_first_time = std::chrono::system_clock::now();
	}

protected:
	virtual void _p_one_iteration() override {
		if (_m_sensor_data_it != _m_sensor_data.end()) {

			ullong dataset_now = _m_sensor_data_it->first;
			reliable_sleep(std::chrono::nanoseconds{dataset_now - dataset_first_time} + real_first_time);
			time_type ts = real_first_time + std::chrono::nanoseconds{dataset_now - dataset_first_time};

			//std::cerr << " IMU time: " << std::chrono::time_point<std::chrono::nanoseconds>(std::chrono::nanoseconds{dataset_now}).time_since_epoch().count() << std::endl;

			const sensor_types& sensor_datum = _m_sensor_data_it->second;
			if (sensor_datum.imu0) {
				_m_imu_cam->put(new imu_cam_type{
					ts,
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

			// last_time = dataset_now;
			++_m_sensor_data_it;
		}
	}

private:
	const std::map<ullong, sensor_types> _m_sensor_data;
	std::map<ullong, sensor_types>::const_iterator _m_sensor_data_it;
	switchboard * const _m_sb;
	std::unique_ptr<writer<imu_cam_type>> _m_imu_cam;
	// ullong last_time;
	ullong dataset_first_time;
	time_type real_first_time;
};

PLUGIN_MAIN(offline_imu_cam)
