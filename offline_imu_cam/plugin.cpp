#include "common/switchboard.hpp"
#include "common/data_format.hpp"
#include "data_loading.hpp"
#include "common/data_format.hpp"
#include "common/threadloop.hpp"
#include "common/global_module_defs.hpp"
#include <cassert>

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
		, _m_imu_integrator{_m_sb->get_writer<imu_integrator_seq>("imu_integrator_seq")}
		, dataset_first_time{_m_sensor_data_it->first}
		, imu_cam_log{record_logger_}
		, camera_cvtfmt_log{record_logger_}
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
	    assert(errno == 0 && "Errno should not be set at start of _p_one_iteration");
		assert(_m_sensor_data_it != _m_sensor_data.end());

		//std::cerr << " IMU time: " << std::chrono::time_point<std::chrono::nanoseconds>(std::chrono::nanoseconds{dataset_now}).time_since_epoch().count() << std::endl;
		time_type real_now = real_first_time + std::chrono::nanoseconds{dataset_now - dataset_first_time};
		const sensor_types& sensor_datum = _m_sensor_data_it->second;
		++_m_sensor_data_it;

		imu_cam_log.log(record{imu_cam_record, {
			{iteration_no},
			{bool(sensor_datum.cam0)},
		}});

		std::optional<cv::Mat*> cam0 = sensor_datum.cam0
			? std::make_optional<cv::Mat*>(sensor_datum.cam0.value().load().release())
			: std::nullopt
			;
		RAC_ERRNO_MSG("offline_imu_cam after cam0");

		std::optional<cv::Mat*> cam1 = sensor_datum.cam1
			? std::make_optional<cv::Mat*>(sensor_datum.cam1.value().load().release())
			: std::nullopt
			;
		RAC_ERRNO_MSG("offline_imu_cam after cam1");

#ifndef NDEBUG
        /// If debugging, assert the image is grayscale
		if (cam0.has_value() && cam1.has_value()) {
		    const int num_ch0 = cam0.value()->channels();
		    const int num_ch1 = cam1.value()->channels();
		    assert(num_ch0 == 1 && "Data from lazy_load_image should be grayscale");
		    assert(num_ch1 == 1 && "Data from lazy_load_image should be grayscale");
		}
#endif /// NDEBUG

		_m_imu_cam.put(new (_m_imu_cam.allocate()) imu_cam_type{
			real_now,
			(sensor_datum.imu0.value().angular_v).cast<float>(),
			(sensor_datum.imu0.value().linear_a).cast<float>(),
			cam0,
			cam1,
			dataset_now,
		};
		_m_imu_cam->put(datum);

		auto imu_integrator_params = new imu_integrator_seq{
			.seq = static_cast<int>(++_imu_integrator_seq),
		};
		_m_imu_integrator->put(imu_integrator_params);

		RAC_ERRNO_MSG("offline_imu_cam at bottom of iteration");
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
    switchboard::writer<imu_integrator_seq> _m_imu_integrator;

	// Timestamp of the first IMU value from the dataset
	ullong dataset_first_time;
	// UNIX timestamp when this component is initialized
	time_type real_first_time;
	// Current IMU timestamp
	ullong dataset_now;

	record_coalescer imu_cam_log;
	record_coalescer camera_cvtfmt_log;
	int64_t _imu_integrator_seq{0};
};

PLUGIN_MAIN(offline_imu_cam)
