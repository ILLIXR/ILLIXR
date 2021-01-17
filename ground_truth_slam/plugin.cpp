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
		, _m_ground_truth_offset{sb->publish<Eigen::Vector3f>("ground_truth_offset")}
		, _m_sensor_data{load_data()}
		, first_time{true}
	{ }

	virtual void start() override {
		plugin::start();
		sb->schedule<imu_cam_type>(id, "imu_cam", [&](const imu_cam_type *datum) {
			this->feed_ground_truth(datum);
		});
	}

	void feed_ground_truth(const imu_cam_type *datum) {
		ullong rounded_time = datum->dataset_time;
		auto it = _m_sensor_data.find(rounded_time);

		if (it == _m_sensor_data.end()) {
#ifndef NDEBUG
				std::cout << "True pose not found at timestamp: " << rounded_time << std::endl;
#endif
			return;
		}

		pose_type* true_pose = new pose_type{it->second};
		true_pose->sensor_time = datum->time;
		// Ground truth position offset is the first ground truth position
		if (first_time) {
			first_time = false;
			_m_ground_truth_offset->put(new Eigen::Vector3f{true_pose->position});
		}
		_m_true_pose->put(true_pose);

#ifndef NDEBUG
		std::cout << "Ground truth pose was found at T: " << rounded_time
				  << " | "
				  << "Pos: ("
				  << true_pose->position[0] << ", "
				  << true_pose->position[1] << ", "
				  << true_pose->position[2] << ")"
				  << " | "
				  << "Quat: ("
				  << true_pose->orientation.w() << ", "
				  << true_pose->orientation.x() << ", "
				  << true_pose->orientation.y() << ","
				  << true_pose->orientation.z() << ")"
				  << std::endl;
#endif
	}

private:
	const std::shared_ptr<switchboard> sb;
	std::unique_ptr<writer<pose_type>> _m_true_pose;
	std::unique_ptr<writer<Eigen::Vector3f>> _m_ground_truth_offset;

	const std::map<ullong, sensor_types> _m_sensor_data;
	bool first_time;
};

PLUGIN_MAIN(ground_truth_slam);
