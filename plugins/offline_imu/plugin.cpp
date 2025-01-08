#include "illixr/data_format/imu.hpp"
#include "illixr/data_loading.hpp"
#include "illixr/managed_thread.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/relative_clock.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"

#include <chrono>

using namespace ILLIXR;
using namespace ILLIXR::data_format;

typedef struct {
    Eigen::Vector3d angular_v;
    Eigen::Vector3d linear_a;
} raw_imu_type;

typedef struct {
    raw_imu_type imu0;
} sensor_types;

inline std::map<ullong, sensor_types> read_data(std::ifstream& gt_file, const std::string& file_name) {
    std::map<ullong, sensor_types> data;

    for (CSVIterator row{gt_file, 1}; row != CSVIterator{}; ++row) {
        ullong          t = std::stoull(row[0]);
        Eigen::Vector3d av{std::stod(row[1]), std::stod(row[2]), std::stod(row[3])};
        Eigen::Vector3d la{std::stod(row[4]), std::stod(row[5]), std::stod(row[6])};
        data[t].imu0 = {av, la};
    }
    return data;
}

class offline_imu : public ILLIXR::threadloop {
public:
    offline_imu(const std::string& name_, phonebook* pb_)
        : threadloop{name_, pb_}
        , _m_sensor_data{load_data<sensor_types>("imu0", "offline_imu", &read_data)}
        , _m_sensor_data_it{_m_sensor_data.cbegin()}
        , _m_imu{_m_sb->get_writer<imu_type>("imu")}
        , dataset_first_time{_m_sensor_data_it->first}
        , dataset_now{0}
        , imu_cam_log{record_logger_}
        , _m_rtc{pb->lookup_impl<RelativeClock>()} { }

protected:
    skip_option _p_should_skip() override {
        if (_m_sensor_data_it != _m_sensor_data.end()) {
            assert(dataset_now < _m_sensor_data_it->first);
            dataset_now = _m_sensor_data_it->first;
            // Sleep for the difference between the current IMU vs 1st IMU and current UNIX time vs UNIX time the component was
            // init
            std::this_thread::sleep_for(std::chrono::nanoseconds{dataset_now - dataset_first_time} -
                                        _m_rtc->now().time_since_epoch());

            return skip_option::run;

        } else {
            return skip_option::stop;
        }
    }

    void _p_one_iteration() override {
        assert(_m_sensor_data_it != _m_sensor_data.end());
        time_point          real_now(std::chrono::duration<long, std::nano>{dataset_now - dataset_first_time});
        const sensor_types& sensor_datum = _m_sensor_data_it->second;

        _m_imu.put(_m_imu.allocate<imu_type>(imu_type{real_now, (sensor_datum.imu0.angular_v), (sensor_datum.imu0.linear_a)}));
        ++_m_sensor_data_it;
    }

private:
    const std::shared_ptr<switchboard>             _m_sb;
    const std::map<ullong, sensor_types>           _m_sensor_data;
    std::map<ullong, sensor_types>::const_iterator _m_sensor_data_it;
    switchboard::writer<imu_type>                  _m_imu;

    // Timestamp of the first IMU value from the dataset
    ullong dataset_first_time;
    // Current IMU timestamp
    ullong dataset_now;

    record_coalescer imu_cam_log;

    std::shared_ptr<RelativeClock> _m_rtc;
};

PLUGIN_MAIN(offline_imu)
