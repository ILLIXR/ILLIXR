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
	, _m_vsync_estimate{sb->subscribe_latest<time_type>("vsync_estimate")}
    {
    	auto newoffset = correct_pose(_m_sensor_data_it->second).orientation;
    	set_offset(newoffset);
    }

    virtual fast_pose_type get_fast_pose() override {
	return get_fast_pose( std::chrono::system_clock::now() );
    }

    virtual pose_type get_true_pose() override {
	throw std::logic_error{"Not Implemented"};
    }


    virtual bool fast_pose_reliable() const override {
	return true;
    }

    virtual bool true_pose_reliable() const override {
	return false;
    }

   virtual void set_offset(const Eigen::Quaternionf& raw_o_times_offset) override{
	std::lock_guard<std::mutex> lock {offset_mutex};
	Eigen::Quaternionf raw_o = raw_o_times_offset * offset.inverse();
	//std::cout << "pose_prediction: set_offset" << std::endl;
	offset = raw_o.inverse();
    }

    Eigen::Quaternionf apply_offset(const Eigen::Quaternionf& orientation) const {
	std::lock_guard<std::mutex> lock {offset_mutex};
	return orientation * offset;
    }
    virtual fast_pose_type get_fast_pose(time_type time) override {
		/*
			ullong vsync_time = std::chrono::nanoseconds(get_vsync(time).time_since_epoch()).count();
			ullong plug_in_time = std::chrono::nanoseconds(_m_start_of_time.time_since_epoch()).count();

			std::cout<<"dataset_first: "<<dataset_first_time<<"vsync time: "<<vsync_time <<" plug_in_time: "<<plug_in_time<<std::endl;
			std::cout<<"lookup time: "<<lookup_time <<std::endl;
			std::cout<<"data end time: "<< _m_sensor_data.rbegin()->first <<std::endl;
		*/
	    	//set_offset(correct_pose(_m_sensor_data.begin()->second).orientation);

		const time_type* estimated_vsync = _m_vsync_estimate->get_latest_ro();
		time_type vsync;
		if(estimated_vsync == nullptr) {
			vsync = std::chrono::system_clock::now();
		} else {
			vsync = *estimated_vsync;
		}

		ullong lookup_time = std::chrono::nanoseconds(vsync - _m_start_of_time ).count() + dataset_first_time;
		ullong  nearest_timestamp;
		if(lookup_time <= _m_sensor_data.begin()->first){
			nearest_timestamp=_m_sensor_data.begin()->first;
		}
		else if (lookup_time >= _m_sensor_data.rbegin()->first){
			nearest_timestamp=_m_sensor_data.rbegin()->first;
		}
		else{
			std::map<ullong, sensor_types>::const_iterator _m_sensor_data_it_local;
			_m_sensor_data_it_local = _m_sensor_data.cbegin();
			ullong prev_timestamp = _m_sensor_data_it_local->first;
			while ( _m_sensor_data_it_local != _m_sensor_data.end() ){
				ullong cur_timestamp = _m_sensor_data_it_local->first;
				if(lookup_time > cur_timestamp){
					prev_timestamp = cur_timestamp;
					++_m_sensor_data_it_local;
				}
				else{
					if((lookup_time - prev_timestamp) >= (cur_timestamp - lookup_time) ){
						nearest_timestamp = cur_timestamp;
						break;
					}
					else{
						nearest_timestamp = prev_timestamp;
						break;
					}
				}
			}
		}
		/*
		std::cout<<"nearest timestamp: "<<nearest_timestamp<<std::endl;
		if(_m_sensor_data.find(nearest_timestamp)!=_m_sensor_data.end())
		{
			std::cout<<"found key"<<std::endl;
		}
		else
		{
			std::cout<<"key not found"<<std::endl;
		}
		auto temp = _m_sensor_data.find(nearest_timestamp)->second;
		std::cout<<temp.position.x()<<", "<<temp.position.y()<<", "<<temp.position.z()<<", "<<temp.orientation.w()<<", "<< temp.orientation.x()<<", "<<temp.orientation.y()<<", "<<temp.orientation.z()<<std::endl;
		*/
		return fast_pose_type{
			.pose = correct_pose(_m_sensor_data.find(nearest_timestamp)->second),
			.imu_time = _m_start_of_time + std::chrono::nanoseconds{nearest_timestamp - dataset_first_time},
			.predict_computed_time = std::chrono::system_clock::now(),
			.predict_target_time = vsync
		};

	}


private:
	const std::shared_ptr<switchboard> sb;

	Eigen::Quaternionf offset {Eigen::Quaternionf::Identity()};
	mutable std::mutex offset_mutex;

	/*pyh: reusing data_loading from ground_truth_slam*/
	const std::map<ullong, sensor_types> _m_sensor_data;
	std::map<ullong, sensor_types>::const_iterator _m_sensor_data_it;
	ullong dataset_first_time;
	time_type _m_start_of_time;
	std::unique_ptr<reader_latest<time_type>> _m_vsync_estimate;

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
