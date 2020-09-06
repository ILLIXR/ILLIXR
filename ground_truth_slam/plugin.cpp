#include <chrono>
#include <iomanip>
#include <thread>
#include "common/plugin.hpp"
#include "common/switchboard.hpp"
#include "common/data_format.hpp"
#include "common/threadloop.hpp"
#include "data_loading.hpp"

using namespace ILLIXR;

class ground_truth_slam : public plugin {
public:
	ground_truth_slam(std::string name_, phonebook* pb_)
		: plugin{name_, pb_}
		, sb{pb->lookup_impl<switchboard>()}
		, _m_true_pose{sb->publish<pose_type>("true_pose")}
		, _m_sensor_data{load_data()}
	{ }

	virtual void start() override {
		plugin::start();
		sb->schedule<imu_cam_type>(get_name(), "imu_cam", [&](const imu_cam_type *datum) {
			this->feed_ground_truth(datum);
		});
	}

	void feed_ground_truth(const imu_cam_type *datum) {
		ullong rounded_time = datum->dataset_time;
		_m_sensor_data_it = _m_sensor_data.find(rounded_time);

		if (_m_sensor_data_it == _m_sensor_data.end()) {
			std::cout << "True pose not found at timestamp: " << rounded_time << std::endl;
			return;
		}

		pose_type* true_pose = new pose_type{_m_sensor_data_it->second};
 		true_pose->time = datum->time;
		// std::cout << "The pose was found at " << true_pose->position[0] << ", " << true_pose->position[1] << ", " << true_pose->position[2] << std::endl; 

		_m_true_pose->put(true_pose);
	}

private:
	const std::shared_ptr<switchboard> sb;
	std::unique_ptr<writer<pose_type>> _m_true_pose;

	const std::map<ullong, sensor_types> _m_sensor_data;
	std::map<ullong, sensor_types>::const_iterator _m_sensor_data_it;
};

PLUGIN_MAIN(ground_truth_slam);
