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
		, _m_true_int_input{sb->get_writer<imu_integrator_input>("true_int_input")}
		, _m_ground_truth_offset{sb->get_writer<switchboard::event_wrapper<Eigen::Vector3f>>("ground_truth_offset")}
		, _m_sensor_data{load_data()}
		, _m_dataset_first_time{_m_sensor_data.cbegin()->first}
		, _m_first_time{true}
	{ }

	virtual void start() override {
		plugin::start();
		sb->schedule<imu_cam_type>(id, "imu_cam", [this](switchboard::ptr<const imu_cam_type> datum, std::size_t) {
			this->feed_ground_truth(datum);
		});
	}

	void feed_ground_truth(switchboard::ptr<const imu_cam_type> datum) {
		ullong rounded_time = datum->time.time_since_epoch().count() + _m_dataset_first_time;
		auto it = _m_sensor_data.find(rounded_time);

		if (it == _m_sensor_data.end()) {
#ifndef NDEBUG
				std::cout << "True pose not found at timestamp: " << rounded_time << std::endl;
#endif
			return;
		}

        switchboard::ptr<imu_integrator_input> true_pose = _m_true_int_input.allocate<imu_integrator_input>(
            imu_integrator_input {
                time_point(duration(std::chrono::nanoseconds(rounded_time))),
                it->second.t_offset,
                it->second.params,
                it->second.biasAcc,
				it->second.biasGyro,
                it->second.position,
				it->second.velocity,
                it->second.quat,
            }
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
    switchboard::writer<switchboard::event_wrapper<Eigen::Vector3f>> _m_ground_truth_offset;
	const std::map<ullong, sensor_types> _m_sensor_data;
    ullong _m_dataset_first_time;
    bool _m_first_time;
};

PLUGIN_MAIN(ground_truth_slam);
