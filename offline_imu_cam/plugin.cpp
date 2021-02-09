#include "common/switchboard.hpp"
#include "common/data_format.hpp"
#include "data_loading.hpp"
#include "common/data_format.hpp"
#include "common/threadloop.hpp"
#include "common/realtime_clock.hpp"

using namespace ILLIXR;

const record_header imu_cam_record {
	"imu_cam",
	{
		{"iteration_no", typeid(std::size_t)},
		{"has_camera", typeid(bool)},
	},
};

class offline_imu_cam : public ILLIXR::threadloop {
public:
	offline_imu_cam(std::string name_, phonebook* pb_)
		: threadloop{name_, pb_}
		, _m_sensor_data{load_data()}
		, _m_sensor_data_it{_m_sensor_data.cbegin()}
		, _m_sb{pb->lookup_impl<switchboard>()}
		, _m_imu_cam{_m_sb->get_writer<imu_cam_type>("imu_cam")}
		, dataset_first_time{_m_sensor_data_it->first}
		, imu_cam_log{record_logger_}
		, camera_cvtfmt_log{record_logger_}
		, _m_rtc{pb->lookup_impl<realtime_clock>()}
	{ }

protected:
	virtual skip_option _p_should_skip() override {
		if (_m_sensor_data_it != _m_sensor_data.end()) {
			dataset_now = _m_sensor_data_it->first;
			// Sleep for the difference between the current IMU vs 1st IMU and current UNIX time vs UNIX time the component was init
			std::this_thread::sleep_for(
				std::chrono::nanoseconds{dataset_now - dataset_first_time}
				- _m_rtc->time_since_start()
			);

			if (_m_sensor_data_it->second.imu0) {
				return skip_option::run;
			} else {
				++_m_sensor_data_it;
				return skip_option::skip_and_yield;
			}

		} else {
			return skip_option::stop;
		}
	}

	virtual void _p_thread_setup() override {
		// std::this_thread::sleep_for(std::chrono::seconds{5});
	}

	virtual void _p_one_iteration() override {
		assert(_m_sensor_data_it != _m_sensor_data.end());
		time_point real_now = _m_rtc->get_start() + std::chrono::nanoseconds{dataset_now - dataset_first_time};
		const sensor_types& sensor_datum = _m_sensor_data_it->second;
		++_m_sensor_data_it;

		imu_cam_log.log(record{imu_cam_record, {
			{iteration_no},
			{bool(sensor_datum.cam0)},
		}});


		std::optional<cv::Mat> cam0 = sensor_datum.cam0
			? std::make_optional<cv::Mat>(sensor_datum.cam0.value().load())
			: std::nullopt
			;
		std::optional<cv::Mat> cam1 = sensor_datum.cam1
			? std::make_optional<cv::Mat>(sensor_datum.cam1.value().load())
			: std::nullopt
			;

		_m_imu_cam.put(new (_m_imu_cam.allocate()) imu_cam_type{
			real_now,
				cam1 ? real_now : time_point{},
			(sensor_datum.imu0.value().angular_v).cast<float>(),
			(sensor_datum.imu0.value().linear_a).cast<float>(),
			cam0,
			cam1,
			dataset_now,
		});
	}

private:
	const std::map<ullong, sensor_types> _m_sensor_data;
	std::map<ullong, sensor_types>::const_iterator _m_sensor_data_it;
	const std::shared_ptr<switchboard> _m_sb;
	switchboard::writer<imu_cam_type> _m_imu_cam;

	// Timestamp of the first IMU value from the dataset
	ullong dataset_first_time;
	// Current IMU timestamp
	ullong dataset_now;

	record_coalescer imu_cam_log;
	record_coalescer camera_cvtfmt_log;
	std::shared_ptr<realtime_clock> _m_rtc;
};

PLUGIN_MAIN(offline_imu_cam)
