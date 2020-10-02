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
        , _m_sensor_data_it{_m_sensor_data.cbegin()}
        , dataset_first_time{_m_sensor_data_it->first}
        , _m_start_of_time{std::chrono::high_resolution_clock::now()}
        , _m_vsync_estimate{sb->get_reader<switchboard::event_wrapper<time_type>>("vsync_estimate")}
    {
    	auto newoffset = correct_pose(_m_sensor_data.begin()->second).orientation;
    	set_offset(newoffset);
    }

    virtual fast_pose_type get_fast_pose() const override {
		switchboard::ptr<const switchboard::event_wrapper<time_type>> estimated_vsync = _m_vsync_estimate.get_nullable();
		time_type vsync;
		if(!estimated_vsync) {
			std::cerr << "Vsync estimation not valid yet, returning fast_pose for now()" << std::endl;
			vsync = std::chrono::system_clock::now();
		} else {
			vsync = **estimated_vsync;
		}
		return vsync;
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

	virtual Eigen::Quaternionf get_offset() override {
        return offset;
    }

	virtual pose_type correct_pose(const pose_type pose) const override {
		pose_type swapped_pose;

		// This uses the OpenVINS standard output coordinate system.
		// This is a mapping between the OV coordinate system and the OpenGL system.
		swapped_pose.position.x() = -pose.position.y();
		swapped_pose.position.y() = pose.position.z();
		swapped_pose.position.z() = -pose.position.x();

		// There is a slight issue with the orientations: basically,
		// the output orientation acts as though the "top of the head" is the
		// forward direction, and the "eye direction" is the up direction.
		Eigen::Quaternionf raw_o (pose.orientation.w(), -pose.orientation.y(), pose.orientation.z(), -pose.orientation.x());

		swapped_pose.orientation = apply_offset(raw_o);

		return swapped_pose;
	}

    virtual void set_offset(const Eigen::Quaternionf& raw_o_times_offset) override{
		std::unique_lock lock {offset_mutex};
		Eigen::Quaternionf raw_o = raw_o_times_offset * offset.inverse();
		//std::cout << "pose_prediction: set_offset" << std::endl;
		offset = raw_o.inverse();
    }

    Eigen::Quaternionf apply_offset(const Eigen::Quaternionf& orientation) const {
		std::shared_lock lock {offset_mutex};
		return orientation * offset;
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

		auto looked_up_pose = nearest_row->second;
		looked_up_pose.sensor_time = _m_start_of_time + std::chrono::nanoseconds{nearest_row->first - dataset_first_time};
		return fast_pose_type{
			.pose = correct_pose(looked_up_pose),
			.predict_computed_time = std::chrono::system_clock::now(),
			.predict_target_time = time
		};

	}


private:
	const std::shared_ptr<switchboard> sb;

	mutable Eigen::Quaternionf offset {Eigen::Quaternionf::Identity()};
	mutable std::shared_mutex offset_mutex;

	/*pyh: reusing data_loading from ground_truth_slam*/
	const std::map<ullong, sensor_types> _m_sensor_data;
	ullong dataset_first_time;
	time_type _m_start_of_time;
	switchboard::reader<switchboard::event_wrapper<time_type>> _m_vsync_estimate;

	pose_type correct_pose(const pose_type pose) const {
		pose_type swapped_pose;

		// This uses the OpenVINS standard output coordinate system.
		// This is a mapping between the OV coordinate system and the OpenGL system.
		swapped_pose.position.x() = -pose.position.y();
		swapped_pose.position.y() = pose.position.z();
		swapped_pose.position.z() = -pose.position.x();

		// There is a slight issue with the orientations: basically,
		// the output orientation acts as though the "top of the head" is the
		// forward direction, and the "eye direction" is the up direction.
	Eigen::Quaternionf raw_o (pose.orientation.w(), -pose.orientation.y(), pose.orientation.z(), -pose.orientation.x());

	swapped_pose.orientation = apply_offset(raw_o);

		return swapped_pose;
	}

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
