#include "illixr/switchboard.hpp"
#include "illixr/data_format.hpp"
#include "data_loading.hpp"
#include "illixr/data_format.hpp"
#include "illixr/threadloop.hpp"
#include "illixr/global_module_defs.hpp"
#include <cassert>

using namespace ILLIXR;

const record_header imu_cam_record {
	"imu_cam",
		{
			{"iteration_no", typeid(std::size_t)},
			{"has_depth", typeid(bool)},
			{"has_color", typeid(bool)},
		},
};

class offline_TUM : public ILLIXR::threadloop {
	public:
		offline_TUM(std::string name_, phonebook* pb_)
			: threadloop{name_, pb_}
		, _m_sensor_data{load_data()}
		, _m_sensor_data_it{_m_sensor_data.cbegin()}
		, _m_sb{pb->lookup_impl<switchboard>()}
		//pyh: subscribing to rgb_depth_pose_type, a format I created for TUM & VCU_RVI, perhaps a portable format can be created
		, _m_rgb_depth_pose{_m_sb->get_writer<rgb_depth_pose_type>("rgb_depth_pose")} 
		, dataset_first_time{_m_sensor_data_it->first}
        , dataset_prev{_m_sensor_data_it->first}
		, imu_cam_log{record_logger_}
		, camera_cvtfmt_log{record_logger_}
		{}

	protected:
		virtual skip_option _p_should_skip() override {
			if (_m_sensor_data_it != _m_sensor_data.end()) {
				dataset_now = _m_sensor_data_it->first;
				std::this_thread::sleep_for(
						std::chrono::nanoseconds{dataset_now - dataset_first_time}
						+ real_first_time
						- std::chrono::high_resolution_clock::now()
						);
				//in the TUM case, since we have no actual imu reading we use depth image to filter
				if (_m_sensor_data_it->second.cam0) {
					return skip_option::run;
				} else {
					++_m_sensor_data_it;
					return skip_option::skip_and_yield;
				}

			} else {
                std::cout<<"stopped due to end of frame\n";
				return skip_option::stop;
				//return skip_option::skip_and_yield;
			}
		}

		virtual void _p_one_iteration() override {
			RAC_ERRNO_MSG("offline_TUM at start of _p_one_iteration");
			assert(_m_sensor_data_it != _m_sensor_data.end());
#ifndef NDEBUG
			std::chrono::time_point<std::chrono::nanoseconds> tp_dataset_now{std::chrono::nanoseconds{dataset_now}};
			//std::cerr << " IMU time: " << tp_dataset_now.time_since_epoch().count() << std::endl;
#endif /// NDEBUG
            // pyh: some debugging codes (best to follow sam's format above)
			//		time_type real_now = real_first_time + std::chrono::nanoseconds{dataset_now - dataset_first_time};
			//		std::cout<<"dataset now: "<<dataset_now<<" count: "<<count<<std::endl;
			
            
            count++;
			const sensor_types& sensor_datum = _m_sensor_data_it->second;
			++_m_sensor_data_it;

			imu_cam_log.log(record{imu_cam_record, {
					{iteration_no},
					{bool(sensor_datum.cam0)},
					{bool(sensor_datum.cam1)},
					}});

			std::optional<cv::Mat> depth = sensor_datum.cam0
				? std::make_optional<cv::Mat>(*(sensor_datum.cam0.value().unmodified_load().release()))
				: std::nullopt
				;
			RAC_ERRNO_MSG("offline_TUM after depth");
			std::optional<cv::Mat> rgb = sensor_datum.cam1
				? std::make_optional<cv::Mat>(*(sensor_datum.cam1.value().modified_load().release()))
				//? std::make_optional<cv::Mat>(*(sensor_datum.cam1.value().unmodified_load().release()))
				: std::nullopt
				;
			RAC_ERRNO_MSG("offline_TUM after rgb");
			std::optional<Eigen::Vector3f> position = sensor_datum.position;
			std::optional<Eigen::Quaternionf> orientation =sensor_datum.orientation;
		
            //pyh: send the data structure to the scoreboard    
			_m_rgb_depth_pose.put(_m_rgb_depth_pose.allocate<rgb_depth_pose_type>(
						rgb_depth_pose_type {
						rgb,
						depth,
						position,
						orientation,
						dataset_now
						}
						));
			RAC_ERRNO_MSG("offline_TUM at bottom of iteration");
            dataset_prev = dataset_now;
		}	

	public:
		virtual void _p_thread_setup() override {
			// this is not done in the constructor, because I want it to
			// be done at thread-launch time, not load-time.
			auto now = std::chrono::system_clock::now();
			real_first_time = std::chrono::time_point_cast<std::chrono::seconds>(now);
            //pyh: some debug code
            //std::cout<<"real first time: "<<real_first_time.time_since_epoch().count()<<"\n";
		}

	private:
		const std::map<ullong, sensor_types> _m_sensor_data;
		std::map<ullong, sensor_types>::const_iterator _m_sensor_data_it;
		
		const std::shared_ptr<switchboard> _m_sb;
		// pyh: data structure for holding incomign frame
		switchboard::writer<rgb_depth_pose_type> _m_rgb_depth_pose;

		// Timestamp of the first IMU value from the dataset
		ullong dataset_first_time;
		// UNIX timestamp when this component is initialized
		time_type real_first_time;
		// Current IMU timestamp
		ullong dataset_now;
		ullong dataset_prev=0;
		//pyh: track # of frames triggered 
		unsigned count=0;
		record_coalescer imu_cam_log;
		record_coalescer camera_cvtfmt_log;
};

PLUGIN_MAIN(offline_TUM)
