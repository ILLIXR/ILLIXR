#include "common/component.hh"
#include "common/switchboard.hh"
#include "common/data_format.hh"
#include "data_loading.hh"
#include "common/data_format.hh"
#include <chrono>

using namespace ILLIXR;

const size_t TIME_SAFETY_FACTOR = 100;

template< class Rep, class Period >
static void reliable_sleep(const std::chrono::duration<Rep, Period>& sleep_duration) {
	auto start = std::chrono::high_resolution_clock::now();
	while (std::chrono::high_resolution_clock::now() - start < sleep_duration - sleep_duration / TIME_SAFETY_FACTOR) {
		std::this_thread::sleep_for(sleep_duration / TIME_SAFETY_FACTOR);
	}
}

const std::string data_path = "data/";

class ground_truth_slam : public component {
public:
	ground_truth_slam(switchboard* sb_)
		: sb{sb_}
		, _m_true_pose{sb->publish<pose_type>("true_pose")}
		, _m_sensor_data{load_data(data_path)}
		, _m_sensor_data_it{_m_sensor_data.cbegin()}
	{
		dataset_first_time = dataset_last_time = _m_sensor_data_it->first;
		real_first_time = std::chrono::system_clock::now();
	}

	virtual void _p_compute_one_iteration() override {
		ullong dataset_now = _m_sensor_data_it->first;

		// TODO: sleep until NOT sleep for
		// Sleeping until an absolute time will mitigate drift
		reliable_sleep(std::chrono::nanoseconds{dataset_now - dataset_last_time});

		// not exactly now,
		// but now in the dataset transposed to now in realtime
		time_type real_nowish =
			real_first_time + std::chrono::nanoseconds{dataset_now - dataset_first_time};
		// real_nowish = std::chrono::system_clock::now();
		pose_type *p = new pose_type{_m_sensor_data_it->second};
		p->time = real_nowish;
		_m_true_pose->put(p);

		dataset_last_time = dataset_now;
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
	ullong dataset_last_time;
	ullong dataset_first_time;
	time_type real_first_time;
};

extern "C" component* create_component(switchboard* sb) {
	return new ground_truth_slam {sb};
}
