#include <ratio>
#include "common/switchboard.hpp"
#include "common/data_format.hpp"
#include "data_loading.hpp"
#include "common/data_format.hpp"
#include "common/threadloop.hpp"
#include "common/global_module_defs.hpp"
#include <cassert>
#include "common/relative_clock.hpp"

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
		, _m_clock{pb->lookup_impl<RelativeClock>()}
	{ }

protected:

	virtual skip_option _p_should_skip() override {
		if (_m_sensor_data_it != _m_sensor_data.end()) {
			dataset_now = _m_sensor_data_it->first;

			std::this_thread::sleep_for(
				RelativeClock::time_point{std::chrono::nanoseconds{dataset_now - dataset_first_time}} - _m_clock->now()
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
	    RAC_ERRNO_MSG("offline_imu_cam at start of _p_one_iteration");
		assert(_m_sensor_data_it != _m_sensor_data.end());
		const sensor_types& sensor_datum = _m_sensor_data_it->second;
		++_m_sensor_data_it;

		imu_cam_log.log(record{imu_cam_record, {
			{iteration_no},
			{bool(sensor_datum.cam0)},
		}});

		std::optional<cv::Mat> cam0 = sensor_datum.cam0
			? std::make_optional<cv::Mat>(*(sensor_datum.cam0.value().load().release()))
			: std::nullopt
			;
		RAC_ERRNO_MSG("offline_imu_cam after cam0");

		std::optional<cv::Mat> cam1 = sensor_datum.cam1
			? std::make_optional<cv::Mat>(*(sensor_datum.cam1.value().load().release()))
			: std::nullopt
			;
		RAC_ERRNO_MSG("offline_imu_cam after cam1");

#ifndef NDEBUG
        /// If debugging, assert the image is grayscale
		if (cam0.has_value() && cam1.has_value()) {
		    const int num_ch0 = cam0.value().channels();
		    const int num_ch1 = cam1.value().channels();
		    assert(num_ch0 == 1 && "Data from lazy_load_image should be grayscale");
		    assert(num_ch1 == 1 && "Data from lazy_load_image should be grayscale");
		}
#endif /// NDEBUG

        _m_imu_cam.put(_m_imu_cam.allocate<imu_cam_type>(
            imu_cam_type {
				time_point{std::chrono::nanoseconds(dataset_now - dataset_first_time)},
                (sensor_datum.imu0.value().angular_v).cast<float>(),
                (sensor_datum.imu0.value().linear_a).cast<float>(),
                cam0,
                cam1
            }
        ));

		RAC_ERRNO_MSG("offline_imu_cam at bottom of iteration");
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
	std::shared_ptr<const RelativeClock> _m_clock;
};

PLUGIN_MAIN(offline_imu_cam)
