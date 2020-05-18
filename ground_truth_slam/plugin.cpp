#include <chrono>
#include <iomanip>
#include <thread>
#include "common/plugin.hpp"
#include "common/switchboard.hpp"
#include "common/data_format.hpp"
#include "common/threadloop.hpp"
#include "common/pose_correction.hpp"
#include "data_loading.hpp"

using namespace ILLIXR;

const std::string data_path = "data1/";

class ground_truth_slam : public plugin {
public:
	ground_truth_slam(phonebook* pb)
		: sb{pb->lookup_impl<switchboard>()}
		, pc{pb->lookup_impl<pose_correction>()}
		, _m_true_pose{sb->publish<pose_type>("true_pose")}
		, _m_sensor_data{load_data(data_path)}
	{}

	virtual void start() override {
   sb->schedule<imu_cam_type>("imu_cam", [&](const imu_cam_type *datum) {
        this->feed_ground_truth(datum);
    });
	}


	void feed_ground_truth(const imu_cam_type *datum) {
		ullong rounded_time = floor(datum->dataset_time / 10000);
		_m_sensor_data_it = _m_sensor_data.find(rounded_time);

		if (_m_sensor_data_it == _m_sensor_data.end()) {
			std::cout << "True pose not found at timestamp: " << rounded_time << std::endl;
			return;
		}

		pose_type* true_pose = new pose_type{_m_sensor_data_it->second};
		true_pose->time = datum->time;
		// std::cout << "The pose was found at " << true_pose->position[0] << ", " << true_pose->position[1] << ", " << true_pose->position[2] << std::endl; 

		_m_true_pose->put(pc->correct_pose(true_pose));
	}

	virtual ~ground_truth_slam() override {}

private:
	switchboard* const sb;
	pose_correction* const pc;
	std::unique_ptr<writer<pose_type>> _m_true_pose;

	const std::map<ullong, sensor_types> _m_sensor_data;
	std::map<ullong, sensor_types>::const_iterator _m_sensor_data_it;
};

PLUGIN_MAIN(ground_truth_slam);
