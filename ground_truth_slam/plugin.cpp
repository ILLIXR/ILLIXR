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
		, _m_true_pose{sb->get_writer<pose_type>("true_pose")}
		, _m_ground_truth_offset{sb->get_writer<Eigen::Vector3f>("ground_truth_offset")}
		, _m_sensor_data{load_data()}
	{ }

	virtual void start() override {
		plugin::start();
		sb->schedule<imu_cam_type>(id, "imu_cam", [this](switchboard::ptr<const imu_cam_type> datum, std::size_t) {
			this->feed_ground_truth(datum);
		});
	}

	void feed_ground_truth(switchboard::ptr<const imu_cam_type> datum) {
		ullong rounded_time = datum->dataset_time;
		auto it = _m_sensor_data.find(rounded_time);

		if (it == _m_sensor_data.end()) {
#ifndef NDEBUG
				std::cout << "True pose not found at timestamp: " << rounded_time << std::endl;
#endif
			return;
		}

		pose_type* true_pose = new (_m_true_pose.allocate()) pose_type{_m_sensor_data_it->second};
		true_pose->sensor_time = datum->time;

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

		_m_true_pose.put(true_pose);
	}

private:
	const std::shared_ptr<switchboard> sb;
	switchboard::writer<pose_type> _m_true_pose;
    switchboard::writer<Eigen::Vector3f> _m_ground_truth_offset;

	const std::map<ullong, sensor_types> _m_sensor_data;
};

PLUGIN_MAIN(ground_truth_slam);
