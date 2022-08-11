#include <chrono>
#include <iomanip>
#include <thread>
#include <filesystem>
#include <fstream>
#include "common/plugin.hpp"

#include "common/data_format.hpp"
#include "common/switchboard.hpp"
#include "common/threadloop.hpp"
#include "data_loading.hpp"

#include <chrono>
#include <iomanip>
#include <thread>

using namespace ILLIXR;

class ground_truth_slam : public plugin {
public:
	ground_truth_slam(std::string name_, phonebook* pb_)
		: plugin{name_, pb_}
		, sb{pb->lookup_impl<switchboard>()}
		, _m_true_pose{sb->get_writer<pose_type>("true_pose")}
		, _m_ground_truth_offset{sb->get_writer<switchboard::event_wrapper<Eigen::Vector3f>>("ground_truth_offset")}
		, _m_sensor_data{load_data()}
		, _m_dataset_first_time{_m_sensor_data.cbegin()->first}
		, _m_first_time{true}
	{
		if (!std::filesystem::exists(data_path)) {
			if (!std::filesystem::create_directory(data_path)) {
				std::cerr << "Failed to create data directory.";
			}
		}
		truth_csv.open(data_path + "/truth.csv");
	}

<<<<<<< HEAD
<<<<<<< HEAD
    virtual void start() override {
        plugin::start();
=======
	virtual void start() override {
		plugin::start();
		sb->schedule<imu_cam_type_prof>(id, "imu_cam", [this](switchboard::ptr<const imu_cam_type_prof> datum, std::size_t) {
>>>>>>> 648ee3394187243ac3de7d8defd8f8bcb6346d05
			this->feed_ground_truth(datum);
		});
	}

	void feed_ground_truth(switchboard::ptr<const imu_cam_type_prof> datum) {
		ullong rounded_time = datum->time.time_since_epoch().count() + _m_dataset_first_time;
		auto it = _m_sensor_data.find(rounded_time);
>>>>>>> WIP add prints, test aws server, packet dropping, and zed freq

        if (it == _m_sensor_data.end()) {
            std::cout << "True pose not found at timestamp: " << rounded_time << std::endl;
#endif
            return;
        }

        switchboard::ptr<pose_type> true_pose = _m_true_pose.allocate<pose_type>(
            pose_type {
                time_point{datum->time},
                it->second.position,
                it->second.orientation
            }
        );
#ifndef NDEBUG
        std::cout << "Ground truth pose was found at T: " << rounded_time << " | "
                  << "Pos: (" << true_pose->position[0] << ", " << true_pose->position[1] << ", " << true_pose->position[2]
                  << ")"
                  << " | "
                  << "Quat: (" << true_pose->orientation.w() << ", " << true_pose->orientation.x() << ", "
                  << true_pose->orientation.y() << "," << true_pose->orientation.z() << ")" << std::endl;
#endif
        

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

        /// Ground truth position offset is the first ground truth position
		if (_m_first_time) {
			_m_first_time = false;
			_m_ground_truth_offset.put(_m_ground_truth_offset.allocate<switchboard::event_wrapper<Eigen::Vector3f>>(
			    true_pose->position
			));
		}

		_m_true_pose.put(std::move(true_pose));
		truth_csv << datum->time.time_since_epoch().count() << ","
				  << true_pose->position.x() << ","
				  << true_pose->position.y() << ","
				  << true_pose->position.z() << ","
				  << true_pose->orientation.w() << ","
				  << true_pose->orientation.x() << ","
				  << true_pose->orientation.y() << ","
				  << true_pose->orientation.z() << std::endl;
	}

private:
	const std::shared_ptr<switchboard> sb;
	switchboard::writer<pose_type> _m_true_pose;

    switchboard::writer<switchboard::event_wrapper<Eigen::Vector3f>> _m_ground_truth_offset;
	const std::map<ullong, sensor_types> _m_sensor_data;
    ullong _m_dataset_first_time;
    bool _m_first_time;

	const std::string data_path = std::filesystem::current_path().string() + "/recorded_data";
	std::ofstream truth_csv;
};

PLUGIN_MAIN(ground_truth_slam);
