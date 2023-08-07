#include "common/data_format.hpp"
#include "common/global_module_defs.hpp"
#include "common/relative_clock.hpp"
#include "common/switchboard.hpp"
#include "common/threadloop.hpp"
#include "data_loading.hpp"

#include <cassert>
#include <ratio>

using namespace ILLIXR;

class offline_imu_cam : public ILLIXR::threadloop {
public:
    offline_imu_cam(std::string name_, phonebook* pb_)
        : threadloop{name_, pb_}
        , _m_sensor_data{load_data()}
        , _m_sensor_data_it{_m_sensor_data.cbegin()}
        , _m_sb{pb->lookup_impl<switchboard>()}
        , _m_clock{pb->lookup_impl<RelativeClock>()}
        , _m_imu{_m_sb->get_writer<imu_type>("imu")}
        , _m_cam{_m_sb->get_writer<cam_type>("cam")}
        , dataset_first_time{_m_sensor_data_it->first}
        , imu_cam_log{record_logger_}
        , camera_cvtfmt_log{record_logger_} { }

protected:
    virtual skip_option _p_should_skip() override {
        if (_m_sensor_data_it != _m_sensor_data.end()) {
            dataset_now = _m_sensor_data_it->first;

            std::this_thread::sleep_for(time_point{std::chrono::nanoseconds{dataset_now - dataset_first_time}} -
                                        _m_clock->now());
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
#ifndef NDEBUG
        std::chrono::time_point<std::chrono::nanoseconds> tp_dataset_now{std::chrono::nanoseconds{dataset_now}};
        std::cerr << " IMU time: " << tp_dataset_now.time_since_epoch().count() << std::endl;
#endif
        const sensor_types& sensor_datum = _m_sensor_data_it->second;
        ++_m_sensor_data_it;

        if (sensor_datum.cam0 && sensor_datum.cam1) {
            cv::Mat cam0 = *(sensor_datum.cam0.value().load().release());
            cv::Mat cam1 = *(sensor_datum.cam1.value().load().release());
            _m_cam.put(_m_cam.allocate<cam_type>(
                cam_type{time_point{std::chrono::nanoseconds(dataset_now - dataset_first_time)},
                        cam0,
                        cam1
                }
            ));
        }

        _m_imu.put(_m_imu.allocate<imu_type>(
            imu_type{time_point{std::chrono::nanoseconds(dataset_now - dataset_first_time)},
                    (sensor_datum.imu0.value().angular_v),
                    (sensor_datum.imu0.value().linear_a)}
        ));

        RAC_ERRNO_MSG("offline_imu_cam at bottom of iteration");
    }

private:
    const std::map<ullong, sensor_types>           _m_sensor_data;
    std::map<ullong, sensor_types>::const_iterator _m_sensor_data_it;
    const std::shared_ptr<switchboard>             _m_sb;
    std::shared_ptr<const RelativeClock>           _m_clock;
    switchboard::writer<imu_type>                  _m_imu;
    switchboard::writer<cam_type>                  _m_cam;

    // Timestamp of the first IMU value from the dataset
    ullong dataset_first_time;
    // Current IMU timestamp
    ullong dataset_now;

    record_coalescer imu_cam_log;
    record_coalescer camera_cvtfmt_log;
};

PLUGIN_MAIN(offline_imu_cam)

