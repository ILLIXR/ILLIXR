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
		, last_ts{0}
		, _m_rtc{pb->lookup_impl<realtime_clock>()}
    { }

	virtual void _p_one_iteration() override {
		duration time_since_start = _m_rtc->time_since_start();
		ullong lookup_time = std::chrono::nanoseconds{time_since_start}.count() + dataset_first_time;
		
		auto after_row = _m_sensor_data.upper_bound(lookup_time);
		std::map<ullong, sensor_types>::const_iterator nearest_row;

		if (after_row == _m_sensor_data.cend() || !(after_row->second.cam0 && after_row->second.cam1)) {
#ifndef NDEBUG
			std::cerr << "Time " << lookup_time << " (" << _m_rtc->time_since_start().count() << " + " << dataset_first_time << ") after last datum " << _m_sensor_data.rbegin()->first << std::endl;
#endif
			after_row--;
			stop();
			return;
		} else if (after_row == _m_sensor_data.cbegin()) {
#ifndef NDEBUG
			std::cerr << "Time " << lookup_time << " (" << _m_rtc->time_since_start().count() << " + " << dataset_first_time << ") before first datum " << _m_sensor_data.cbegin()->first << std::endl;
#endif
		} else {
			// "std::map::upper_bound" returns an iterator to the first pair whose key is GREATER than the argument.
			// I already know we aren't at the begin()

			// Most recent row
			// after_row--;
			// nearest_row = after_row /* after_row - 1 */;

			// Nearest row
			auto after_row2 = after_row;
			auto prev_row = after_row; prev_row--;
			// stop using after_row
			if (after_row2->first - lookup_time < lookup_time - prev_row->first) {
				nearest_row = after_row2;
			} else {
				nearest_row = prev_row;
			}
		}

		

		if (last_ts != nearest_row->first) {
			last_ts = nearest_row->first;
			_m_cam_publisher.put(new (_m_cam_publisher.allocate()) cam_type {
				time_since_start + _m_rtc->get_start(),
				nearest_row->second.cam0.value().load(),
				nearest_row->second.cam1.value().load(),
				nearest_row->first
			});
			good++;
		} else {
			bad++;
			// std::cerr
			// 	<< "Last image published at: " << last_ts - dataset_first_time << "\n"
			// 	<< "Now is: " << std::chrono::nanoseconds(time_since_start).count() << "\n"
			// 	<< "Diff is " << std::chrono::nanoseconds(time_since_start - std::chrono::nanoseconds{last_ts - dataset_first_time}).count() << "\n"
			// 	<< "Therefore, I'm skipping publishing a camera image.\n"
			// 	<< "Ratio is " << good << ":" << bad << "\n"
			// 	;
			// abort();
		}
		std::this_thread::sleep_for(
			_m_rtc->time_since_start() - std::chrono::nanoseconds{nearest_row->first} + std::chrono::milliseconds{30}
		);
	}

	size_t good = 0, bad = 0;

private:
	const std::shared_ptr<switchboard> sb;
	switchboard::writer<cam_type> _m_cam_publisher;
	const std::map<ullong, sensor_types> _m_sensor_data;
	ullong dataset_first_time;
	ullong last_ts;
	std::shared_ptr<realtime_clock> _m_rtc;
};

PLUGIN_MAIN(offline_cam);
