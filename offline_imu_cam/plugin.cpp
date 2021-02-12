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

class offline_imu_cam : public ILLIXR::threadloop {
public:
	offline_imu_cam(std::string name_, phonebook* pb_)
		: threadloop{name_, pb_}
		, _m_sensor_data{load_data()}
		, _m_sensor_data_it{_m_sensor_data.cbegin()}
		, _m_sb{pb->lookup_impl<switchboard>()}
		, _m_imu_cam{_m_sb->get_writer<imu_cam_type>("imu_cam")}
		, _m_imu_integrator{_m_sb->get_writer<switchboard::event_wrapper<imu_integrator_seq>>("imu_integrator_seq")}
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
		assert(_m_sensor_data_it != _m_sensor_data.end());
#ifndef NDEBUG
        std::chrono::time_point<std::chrono::nanoseconds> tp_dataset_now{std::chrono::nanoseconds{dataset_now}};
		std::cerr << " IMU time: " << tp_dataset_now.time_since_epoch().count() << std::endl;
#endif /// NDEBUG
		time_type real_now = real_first_time + std::chrono::nanoseconds{dataset_now - dataset_first_time};
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

#ifndef NDEBUG
		if (cam0.has_value() && cam1.has_value()) {
            int         num_ch0   = cam0.value().channels();
            int         depth_id0 = cam0.value().depth();
            std::size_t num_elem0 = cam0.value().total();
            int         num_ch1   = cam1.value().channels();
            int         depth_id1 = cam1.value().depth();
            std::size_t num_elem1 = cam1.value().total();

            std::cerr << "|| Cam0 channels : " << num_ch0   << std::endl  // 1 ?
                      << "|| Cam0 depth    : " << depth_id0 << std::endl  // 0 ?
                      << "|| Cam0 elems    : " << num_elem0 << std::endl  // 360960 ?
                      << "|| Cam1 channels : " << num_ch1   << std::endl  // 1 ?
                      << "|| Cam1 depth    : " << depth_id1 << std::endl  // 0 ?
                      << "|| Cam1 elems    : " << num_elem1 << std::endl; // 360960 ?

            if (num_ch0 > 1 || num_ch1 > 1) {
                std::cerr << "[offline_imu_cam] **Warning** Input camera images are not grayscale. "
                          << "                              Colorscale conversion may be required. "
                          << std::endl;
            }
		}
#endif /// NDEBUG

        imu_cam_type datum_imu_cam_tmp {
			real_now,
			(sensor_datum.imu0.value().angular_v).cast<float>(),
			(sensor_datum.imu0.value().linear_a).cast<float>(),
			cam0,
			cam1,
			dataset_now,
        };
        switchboard::ptr<imu_cam_type> datum_imu_cam = _m_imu_cam.allocate<imu_cam_type>(std::move(datum_imu_cam_tmp));
		_m_imu_cam.put(std::move(datum_imu_cam));

        imu_integrator_seq datum_imu_int_tmp {
			static_cast<int>(++_imu_integrator_seq),
        };
        switchboard::ptr<switchboard::event_wrapper<imu_integrator_seq>> datum_imu_int =
            _m_imu_integrator.allocate<switchboard::event_wrapper<imu_integrator_seq>>(std::move(datum_imu_int_tmp));
		_m_imu_integrator.put(std::move(datum_imu_int));
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
    switchboard::writer<switchboard::event_wrapper<imu_integrator_seq>> _m_imu_integrator;

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
