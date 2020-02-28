#include "common/component.hh"
#include "common/switchboard.hh"
#include "common/data_format.hh"
#include "data_loading.hh"

using namespace ILLIXR;

const std::string data_path = "data/";

class offline_imu_cam : public component {
public:
	offline_imu_cam(
		 std::unique_ptr<writer<std::pair<vec3, vec3>>>&& imu0,
		 std::unique_ptr<writer<cv::Mat>>&& cam0,
		 std::unique_ptr<writer<cv::Mat>>&& cam1)
		: _m_sensor_data{load_data(data_path)}
		, _m_imu0{std::move(imu0)}
		, _m_cam0{std::move(cam0)}
		, _m_cam1{std::move(cam1)}
	{
		time = _m_sensor_data.cbegin()->first;
	}

	virtual void _p_compute_one_iteration() override {
		const sensor_types& sensor_datum = _m_sensor_data.at(time);
		if (sensor_datum.imu0) {
			_m_imu0->put(&sensor_datum.imu0.value());
		}
		if (sensor_datum.cam0) {
			_m_cam0->put(sensor_datum.cam0.value().load());
		}
		if (sensor_datum.cam1) {
			_m_cam1->put(sensor_datum.cam1.value().load());
		}
	}

	virtual ~offline_imu_cam() override { }

private:
	const std::map<double, sensor_types> _m_sensor_data;
	std::unique_ptr<writer<std::pair<vec3, vec3>>> _m_imu0;
	std::unique_ptr<writer<cv::Mat>> _m_cam0;
	std::unique_ptr<writer<cv::Mat>> _m_cam1;
	double time; //@no-pun-intended
};

extern "C" component* create_component(switchboard* sb) {
	auto imu0_ev = sb->publish<std::pair<vec3, vec3>>("imu0");
	auto cam0_ev = sb->publish<cv::Mat>("cam0");
	auto cam1_ev = sb->publish<cv::Mat>("cam1");
	return new offline_imu_cam {std::move(imu0_ev), std::move(cam0_ev), std::move(cam1_ev)};
}
