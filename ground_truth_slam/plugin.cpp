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
		, _m_true_int_input{sb->get_writer<imu_integrator_input>("true_int_input")}
		, _m_ground_truth_offset{sb->get_writer<switchboard::event_wrapper<Eigen::Vector3f>>("ground_truth_offset")}
		, _m_sensor_data{load_data()}
		, _m_dataset_first_time{_m_sensor_data.cbegin()->first}
		, _m_first_time{true}
	{ }

    virtual void start() override {
        plugin::start();
        sb->schedule<imu_type>(id, "imu", [this](switchboard::ptr<const imu_type> datum, std::size_t) {
            this->feed_ground_truth(datum);
        });
    }

    void feed_ground_truth(switchboard::ptr<const imu_type> datum) {
        ullong rounded_time = datum->time.time_since_epoch().count() + _m_dataset_first_time;
        auto   it           = _m_sensor_data.find(rounded_time);

        if (it == _m_sensor_data.end()) {
#ifndef NDEBUG
            std::cout << "True pose not found at timestamp: " << rounded_time << std::endl;
#endif
            return;
        }

<<<<<<< HEAD
        switchboard::ptr<pose_type> true_pose =
            _m_true_pose.allocate<pose_type>(pose_type{time_point{datum->time}, it->second.position, it->second.orientation});

#ifndef NDEBUG
        std::cout << "Ground truth pose was found at T: " << rounded_time << " | "
                  << "Pos: (" << true_pose->position[0] << ", " << true_pose->position[1] << ", " << true_pose->position[2]
                  << ")"
                  << " | "
                  << "Quat: (" << true_pose->orientation.w() << ", " << true_pose->orientation.x() << ", "
                  << true_pose->orientation.y() << "," << true_pose->orientation.z() << ")" << std::endl;
        );
		
        /// Ground truth position offset is the first ground truth position
		if (_m_first_time) {
			_m_first_time = false;
			_m_ground_truth_offset.put(_m_ground_truth_offset.allocate<switchboard::event_wrapper<Eigen::Vector3f>>(
				Eigen::Vector3f{it->second.position[0], it->second.position[1], it->second.position[2]}
			));
		}

		_m_true_int_input.put(std::move(true_pose));
	}

private:
	const std::shared_ptr<switchboard> sb;
	switchboard::writer<imu_integrator_input> _m_true_int_input;
>>>>>>> Modify GT reader to push out biases as well, modify integrator to use GT biases, debugging pose prediction
    switchboard::writer<switchboard::event_wrapper<Eigen::Vector3f>> _m_ground_truth_offset;
    const std::map<ullong, sensor_types>                             _m_sensor_data;
    ullong                                                           _m_dataset_first_time;
    bool                                                             _m_first_time;
};

PLUGIN_MAIN(ground_truth_slam);
