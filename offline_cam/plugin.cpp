#include <shared_mutex>
#include "common/phonebook.hpp"
#include "common/pose_prediction.hpp"
#include "common/data_format.hpp"
#include "common/plugin.hpp"
#include "common/threadloop.hpp"

/*pyh: reusing data_loading from ground_truth_slam*/
#include "data_loading.hpp"

using namespace ILLIXR;

class offline_cam : public threadloop {
public:
    offline_cam(std::string name_, phonebook* pb_)
		: threadloop{name_, pb_}
		, sb{pb->lookup_impl<switchboard>()}
		, _m_cam_publisher{sb->get_writer<cam_type>("cam")}
		, _m_sensor_data{load_data()}
		, dataset_first_time{_m_sensor_data.cbegin()->first}
		, _m_start_of_time{std::chrono::high_resolution_clock::now()}
    { }

	virtual void _p_one_iteration() override {
		time_type time = std::chrono::system_clock::now();
		ullong lookup_time = std::chrono::nanoseconds(time - _m_start_of_time).count() + dataset_first_time;
		
		auto nearest_row = _m_sensor_data.upper_bound(lookup_time);

		if (nearest_row == _m_sensor_data.cend()) {
#ifndef NDEBUG
			std::cerr << "Time " << lookup_time << " (" << std::chrono::nanoseconds(time - _m_start_of_time).count() << " + " << dataset_first_time << ") after last datum " << _m_sensor_data.rbegin()->first << std::endl;
#endif
			nearest_row--;
		} else if (nearest_row == _m_sensor_data.cbegin()) {
#ifndef NDEBUG
			std::cerr << "Time " << lookup_time << " (" << std::chrono::nanoseconds(time - _m_start_of_time).count() << " + " << dataset_first_time << ") before first datum " << _m_sensor_data.cbegin()->first << std::endl;
#endif
		} else {
			// "std::map::upper_bound" returns an iterator to the first pair whose key is GREATER than the argument.
			// I already know we aren't at the begin()
			// So I will decrement nearest_row here.
			nearest_row--;
		}
		assert(nearest_row->second.cam0 && nearest_row->second.cam1);

		if (last_ts != nearest_row->first) {
			last_ts = nearest_row->first;
			_m_cam_publisher.put(new (_m_cam_publisher.allocate()) cam_type {
				time,
					nearest_row->second.cam0.value().load(),
					nearest_row->second.cam1.value().load(),
				nearest_row->first
			});
		}
	}


private:
	const std::shared_ptr<switchboard> sb;
	switchboard::writer<cam_type> _m_cam_publisher;
	const std::map<ullong, sensor_types> _m_sensor_data;
	ullong dataset_first_time;
	time_type _m_start_of_time;
	ullong last_ts;
};

PLUGIN_MAIN(offline_cam);
