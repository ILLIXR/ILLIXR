#include <cmath>
#include <shared_mutex>
#include "common/phonebook.hpp"
#include "common/pose_prediction.hpp"
#include "common/data_format.hpp"
#include "common/plugin.hpp"

/*pyh: reusing data_loading from ground_truth_slam*/
#include "data_loading.hpp"

using namespace ILLIXR;

class pose_lookup_impl : public pose_prediction {
public:
    pose_lookup_impl(const phonebook* const pb)
		: sb{pb->lookup_impl<switchboard>()}
		, _m_sensor_data{load_data()}
		, dataset_first_time{_m_sensor_data.cbegin()->first}
		, _m_start_of_time{std::chrono::high_resolution_clock::now()}
		, _m_vsync_estimate{sb->subscribe_latest<time_type>("vsync_estimate")}
    {
	}

    virtual fast_pose_type get_fast_pose() const override {
		const time_type* estimated_vsync = _m_vsync_estimate->get_latest_ro();
		if(estimated_vsync == nullptr) {
			std::cerr << "Vsync estimation not valid yet, returning fast_pose for now()" << std::endl;
			return get_fast_pose(std::chrono::system_clock::now());
		} else {
			return get_fast_pose(*estimated_vsync);
		}
    }

    virtual pose_type get_true_pose() const override {
		throw std::logic_error{"Not Implemented"};
    }


    virtual bool fast_pose_reliable() const override {
		return true;
    }

    virtual bool true_pose_reliable() const override {
		return false;
    }

    virtual fast_pose_type get_fast_pose(time_type time) const override {
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

		auto pose = gt_transform(nearest_row->second);

		return fast_pose_type{
			.pose = pose,
			.predict_computed_time = std::chrono::system_clock::now(),
			.predict_target_time = time,
		};
	}

private:
	const std::shared_ptr<switchboard> sb;

	/*pyh: reusing data_loading from ground_truth_slam*/
	const std::map<ullong, sensor_types> _m_sensor_data;
	ullong dataset_first_time;
	time_type _m_start_of_time;
	std::unique_ptr<reader_latest<time_type>> _m_vsync_estimate;
};

class pose_lookup_plugin : public plugin {
public:
    pose_lookup_plugin(const std::string& name, phonebook* pb)
    	: plugin{name, pb}
	{
		pb->register_impl<pose_prediction>(
			std::static_pointer_cast<pose_prediction>(
				std::make_shared<pose_lookup_impl>(pb)
			)
		);
	}
};

PLUGIN_MAIN(pose_lookup_plugin);
