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

	const record_header __camera_cvtfmt_header {"camera_cvtfmt", {
		{"iteration_no", typeid(std::size_t)},
		{"cpu_time_start", typeid(std::chrono::nanoseconds)},
		{"cpu_time_stop" , typeid(std::chrono::nanoseconds)},
		{"wall_time_start", typeid(std::chrono::high_resolution_clock::time_point)},
		{"wall_time_stop" , typeid(std::chrono::high_resolution_clock::time_point)},
	}};

class offline_imu_cam : public ILLIXR::threadloop {
public:
	offline_imu_cam(std::string name_, phonebook* pb_)
		: threadloop{name_, pb_}
		, _m_sensor_data{load_data()}
		, _m_sensor_data_it{_m_sensor_data.cbegin()}
		, _m_sb{pb->lookup_impl<switchboard>()}
		, _m_imu_cam{_m_sb->publish<imu_cam_type>("imu_cam")}
		, dataset_first_time{_m_sensor_data_it->first}
		, imu_cam_log{metric_logger}
		, camera_cvtfmt_log{metric_logger}
	{ }

protected:
	virtual skip_option _p_should_skip() override {
		if (_m_sensor_data_it != _m_sensor_data.end()) {
			dataset_now = _m_sensor_data_it->first;
			std::this_thread::sleep_for(
				std::chrono::nanoseconds{dataset_now - dataset_first_time}
				+ real_first_time
				- std::chrono::high_resolution_clock::now()
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

	virtual void _p_one_iteration() override {
		assert(_m_sensor_data_it != _m_sensor_data.end());
		//std::cerr << " IMU time: " << std::chrono::time_point<std::chrono::nanoseconds>(std::chrono::nanoseconds{dataset_now}).time_since_epoch().count() << std::endl;
		time_type real_now = real_first_time + std::chrono::nanoseconds{dataset_now - dataset_first_time};
		const sensor_types& sensor_datum = _m_sensor_data_it->second;
		++_m_sensor_data_it;

		imu_cam_log.log(record{&imu_cam_record, {
			{iteration_no},
			{bool(sensor_datum.cam0)},
		}});


		auto start_cpu_time  = thread_cpu_time();
		auto start_wall_time = std::chrono::high_resolution_clock::now();
		std::optional<cv::Mat*> cam0 = sensor_datum.cam0
			? std::make_optional<cv::Mat*>(sensor_datum.cam0.value().load().release())
			: std::nullopt
			;
		std::optional<cv::Mat*> cam1 = sensor_datum.cam1
			? std::make_optional<cv::Mat*>(sensor_datum.cam1.value().load().release())
			: std::nullopt
			;
		camera_cvtfmt_log.log(record{&__camera_cvtfmt_header, {
			{iteration_no},
			{start_cpu_time},
			{thread_cpu_time()},
			{start_wall_time},
			{std::chrono::high_resolution_clock::now()},
		}});

		_m_imu_cam->put(new imu_cam_type{
			real_now,
			(sensor_datum.imu0.value().angular_v).cast<float>(),
			(sensor_datum.imu0.value().linear_a).cast<float>(),
			cam0,
			cam1,
			dataset_now,
		});
	}

public:
	virtual void _p_thread_setup() override {
		// this is not done in the constructor, because I want it to
		// be done at thread-launch time, not load-time.
		real_first_time = std::chrono::system_clock::now();
	}

private:
	const std::map<ullong, sensor_types> _m_sensor_data;
	std::map<ullong, sensor_types>::const_iterator _m_sensor_data_it;
	const std::shared_ptr<switchboard> _m_sb;
	std::unique_ptr<writer<imu_cam_type>> _m_imu_cam;

	ullong dataset_first_time;
	time_type real_first_time;
	ullong dataset_now;

	metric_coalescer imu_cam_log;
	metric_coalescer camera_cvtfmt_log;
};

PLUGIN_MAIN(offline_imu_cam)
