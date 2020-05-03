#include "common/plugin.hh"
#include "common/plugin.hh"
#include "common/switchboard.hh"
#include "common/data_format.hh"
#include "common/threadloop.hh"
#include "data_loading.hh"
#include "common/data_format.hh"
#include <chrono>
#include <iomanip>
#include <thread>

using namespace ILLIXR;

const std::string data_path = "data1/";

class ground_truth_slam : public threadloop {
public:
	ground_truth_slam(phonebook* pb)
		: sb{pb->lookup_impl<switchboard>()}
		, _m_true_pose{sb->publish<pose_type>("true_pose")}
		, _m_sensor_data{load_data(data_path)}
		, _m_sensor_data_it{_m_sensor_data.cbegin()}
	{
		dataset_first_time = _m_sensor_data_it->first;
		real_first_time = std::chrono::system_clock::now();
	}

	virtual void _p_one_iteration() override {
		ullong dataset_now = _m_sensor_data_it->first;
		reliable_sleep(std::chrono::nanoseconds{dataset_now - dataset_first_time} + real_first_time);

		// not exactly now,
		// but now in the dataset transposed to now in realtime
		time_type real_nowish =
			real_first_time + std::chrono::nanoseconds{dataset_now - dataset_first_time};
		// real_nowish = std::chrono::system_clock::now();
		pose_type *p = new pose_type{_m_sensor_data_it->second};
		p->time = real_nowish;
		_m_true_pose->put(p);
		// std::cout
		// 	<< std::setprecision(2)
		// 	<< p->orientation.w() << ", "
		// 	<< p->orientation.x() << ", "
		// 	<< p->orientation.y() << ", "
		// 	<< p->orientation.z() << ", "
		// 	<< std::endl;

		// dataset_last_time = dataset_now;
		++_m_sensor_data_it;
		if (_m_sensor_data_it == _m_sensor_data.cend()) {
			stop();
		}
	}

	virtual ~ground_truth_slam() override { }

private:
	switchboard* const sb;
	const std::map<ullong, sensor_types> _m_sensor_data;
	std::map<ullong, sensor_types>::const_iterator _m_sensor_data_it;
	std::unique_ptr<writer<pose_type>> _m_true_pose;
	ullong dataset_first_time;
	time_type real_first_time;
};

PLUGIN_MAIN(ground_truth_slam);
