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

class offline_imu : public ILLIXR::threadloop {
public:
	offline_imu(std::string name_, phonebook* pb_)
		: threadloop{name_, pb_, false}
		, _m_sensor_data{load_data()}
		, _m_sensor_data_it{_m_sensor_data.cbegin()}
		, _m_sb{pb->lookup_impl<switchboard>()}
		, _m_imu_cam{_m_sb->get_writer<imu_cam_type>("imu_cam")}
		, dataset_first_time{_m_sensor_data_it->first}
		, dataset_now{0}
		, imu_cam_log{record_logger_}
		, camera_cvtfmt_log{record_logger_}
		, _m_cam{_m_sb->get_reader<cam_type>("cam")}
		, last_cam_ts{0}
		, _m_log{"imu_cam.csv"}
		, _m_rtc{pb->lookup_impl<realtime_clock>()}
	{
		_m_log << "imu_rt,imu_dt,cam_dt\n";
	}

protected:
	virtual skip_option _p_should_skip() override {
		if (_m_sensor_data_it != _m_sensor_data.end()) {
			assert(dataset_now < _m_sensor_data_it->first);
			dataset_now = _m_sensor_data_it->first;
			// Sleep for the difference between the current IMU vs 1st IMU and current UNIX time vs UNIX time the component was init
			std::this_thread::sleep_for(
				std::chrono::nanoseconds{dataset_now - dataset_first_time}
				- _m_rtc->time_since_start()
			);

			return skip_option::run;

		} else {
			return skip_option::stop;
		}
	}

	virtual void _p_one_iteration() override {
		assert(_m_sensor_data_it != _m_sensor_data.end());
		//std::cerr << " IMU time: " << std::chrono::time_point<std::chrono::nanoseconds>(std::chrono::nanoseconds{dataset_now}).time_since_epoch().count() << std::endl;
		time_point real_now = _m_rtc->get_start() + std::chrono::nanoseconds{dataset_now - dataset_first_time};
		const sensor_types& sensor_datum = _m_sensor_data_it->second;

		std::optional<cv::Mat> cam0 = std::nullopt;
		std::optional<cv::Mat> cam1 = std::nullopt;
		time_point cam_time;

		_m_log << (_m_rtc->time_since_start() + std::chrono::nanoseconds{dataset_first_time}).count() << ',' << dataset_now << ',';

		switchboard::ptr<const cam_type> cam = _m_cam.get_ro_nullable();
		if (cam && last_cam_ts != cam->dataset_time) {
			last_cam_ts = cam->dataset_time;
			cam0 = cam->img0;
			cam1 = cam->img1;
			cam_time = cam->time;
			_m_log << cam->dataset_time;
		}
		_m_log << "\n";
		
		imu_cam_log.log(record{imu_cam_record, {
			{iteration_no},
			{bool(cam0)},
		}});

		_m_imu_cam.put(new (_m_imu_cam.allocate()) imu_cam_type{
			real_now,
			cam_time,
			(sensor_datum.imu0.angular_v).cast<float>(),
			(sensor_datum.imu0.linear_a).cast<float>(),
			cam0,
			cam1,
			dataset_now,
		});

		++_m_sensor_data_it;
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

	switchboard::reader<cam_type> _m_cam;
	ullong last_cam_ts;
	std::ofstream _m_log;
	std::shared_ptr<realtime_clock> _m_rtc;
};

PLUGIN_MAIN(offline_imu)
