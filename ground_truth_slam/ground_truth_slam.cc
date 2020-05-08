#include "common/plugin.hh"
#include "common/switchboard.hh"
#include "common/data_format.hh"
#include "common/threadloop.hh"
#include "data_loading.hh"
#include <chrono>
#include <iomanip>
#include <thread>

using namespace ILLIXR;

const std::string data_path = "data1/";

class ground_truth_slam : public plugin {
public:
	ground_truth_slam(phonebook* pb)
		: sb{pb->lookup_impl<switchboard>()}
		, _m_true_pose{sb->publish<pose_type>("true_pose")}
		, _m_sensor_data{load_data(data_path)}
	{
		std::cout << "Called ground truth" << std::endl;
		sb->schedule<imu_cam_type>("imu_cam", std::bind(&ground_truth_slam::feed_ground_truth, this, std::placeholders::_1));
		std::cout << "asdasdasd" << std::endl;
	}

	void feed_ground_truth(const imu_cam_type *datum) {
		_m_sensor_data_it = _m_sensor_data.find(datum->temp_time);

		if (_m_sensor_data_it == _m_sensor_data.end()) {
			_m_true_pose->put(NULL);
			std::cout << "True pose not found at timestamp: " << datum->temp_time << std::endl;
			return;
		}

		pose_type *true_pose = new pose_type{_m_sensor_data_it->second};
		true_pose->time = datum->time;
		std::cout << "The pose was found at " << true_pose->position[0] << std::endl;

		_m_true_pose->put(true_pose);
	}

	virtual ~ground_truth_slam() override {}

private:
	switchboard* const sb;
	std::unique_ptr<writer<pose_type>> _m_true_pose;

	const std::map<ullong, sensor_types> _m_sensor_data;
	std::map<ullong, sensor_types>::const_iterator _m_sensor_data_it;
};

PLUGIN_MAIN(ground_truth_slam);
