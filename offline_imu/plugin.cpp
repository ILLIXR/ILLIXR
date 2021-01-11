#include "common/switchboard.hpp"
#include "common/data_format.hpp"
#include "data_loading.hpp"
#include "common/data_format.hpp"
#include "common/threadloop.hpp"

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
		, imu_cam_log{record_logger_}
		, camera_cvtfmt_log{record_logger_}
		, _m_cam{_m_sb->get_reader<cam_type>("cam")}
		, last_cam_ts{0}
	{ }

protected:
	virtual skip_option _p_should_skip() override {
		if (_m_sensor_data_it != _m_sensor_data.end()) {
			dataset_now = _m_sensor_data_it->first;
			// Sleep for the difference between the current IMU vs 1st IMU and current UNIX time vs UNIX time the component was init
			std::this_thread::sleep_for(
				std::chrono::nanoseconds{dataset_now - dataset_first_time}
				+ real_first_time
				- std::chrono::high_resolution_clock::now()
			);

			++_m_sensor_data_it;
			return skip_option::run;

		} else {
			stop();
			return skip_option::skip_and_yield;
		}
	}

	virtual void _p_one_iteration() override {
		assert(_m_sensor_data_it != _m_sensor_data.end());
		//std::cerr << " IMU time: " << std::chrono::time_point<std::chrono::nanoseconds>(std::chrono::nanoseconds{dataset_now}).time_since_epoch().count() << std::endl;
		time_type real_now = real_first_time + std::chrono::nanoseconds{dataset_now - dataset_first_time};
		const sensor_types& sensor_datum = _m_sensor_data_it->second;
		++_m_sensor_data_it;


		std::optional<cv::Mat> cam0 = std::nullopt;
		std::optional<cv::Mat> cam1 = std::nullopt;

		switchboard::ptr<const cam_type> cam = _m_cam.get_nullable();
		if (cam && last_cam_ts != cam->dataset_time) {
			last_cam_ts = cam->dataset_time;
			cam0 = cam->img0;
			cam1 = cam->img1;
		}
		
		imu_cam_log.log(record{imu_cam_record, {
			{iteration_no},
			{bool(cam0)},
		}});

		_m_imu_cam.put(new (_m_imu_cam.allocate()) imu_cam_type{
			real_now,
			(sensor_datum.imu0.angular_v).cast<float>(),
			(sensor_datum.imu0.linear_a).cast<float>(),
			cam0,
			cam1,
			dataset_now,
		});
	}

public:
	virtual void _p_thread_setup() override {
		// this is not done in the constructor, because I want it to
		// be done at thread-launch time, not load-time.
		auto now = std::chrono::system_clock::now();
		real_first_time = std::chrono::time_point_cast<std::chrono::seconds>(now);
	}

private:
	const std::map<ullong, sensor_types> _m_sensor_data;
	std::map<ullong, sensor_types>::const_iterator _m_sensor_data_it;
	const std::shared_ptr<switchboard> _m_sb;
	switchboard::writer<imu_cam_type> _m_imu_cam;

	// Timestamp of the first IMU value from the dataset
	ullong dataset_first_time;
	// UNIX timestamp when this component is initialized
	time_type real_first_time;
	// Current IMU timestamp
	ullong dataset_now;

	record_coalescer imu_cam_log;
	record_coalescer camera_cvtfmt_log;

	switchboard::reader<cam_type> _m_cam;
	ullong last_cam_ts;
};

PLUGIN_MAIN(offline_imu)
