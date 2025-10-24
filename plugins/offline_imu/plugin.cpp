#include "plugin.hpp"

#include "illixr/data_loading.hpp"

#include <chrono>

using namespace ILLIXR;
using namespace ILLIXR::data_format;

inline std::map<ullong, sensor_types> read_data(std::ifstream& gt_file, const std::string& file_name) {
    (void) file_name;
    std::map<ullong, sensor_types> data;

    for (csv_iterator row{gt_file, 1}; row != csv_iterator{}; ++row) {
        ullong          t = std::stoull(row[0]);
        Eigen::Vector3d av{std::stod(row[1]), std::stod(row[2]), std::stod(row[3])};
        Eigen::Vector3d la{std::stod(row[4]), std::stod(row[5]), std::stod(row[6])};
        data[t].imu0 = {av, la};
    }
    return data;
}

[[maybe_unused]] offline_imu::offline_imu(const std::string& name, phonebook* pb)
    : threadloop{name, pb}
    , switchboard_{phonebook_->lookup_impl<switchboard>()}
    , sensor_data_{load_data<sensor_types>("imu0", "offline_imu", &read_data, switchboard_)}
    , sensor_data_it_{sensor_data_.cbegin()}
    , imu_{switchboard_->get_writer<imu_type>("imu")}
    , dataset_first_time_{sensor_data_it_->first}
    , dataset_now_{0}
    , imu_cam_log_{record_logger_}
    , clock_{phonebook_->lookup_impl<relative_clock>()} { }

ILLIXR::threadloop::skip_option offline_imu::_p_should_skip() {
    if (sensor_data_it_ != sensor_data_.end()) {
        assert(dataset_now_ < sensor_data_it_->first);
        dataset_now_ = sensor_data_it_->first;
        // Sleep for the difference between the current IMU vs 1st IMU and current UNIX time vs UNIX time the component was
        // init
        std::this_thread::sleep_for(std::chrono::nanoseconds{dataset_now_ - dataset_first_time_} -
                                    clock_->now().time_since_epoch());

        return skip_option::run;

    } else {
        return skip_option::stop;
    }
}

void offline_imu::_p_one_iteration() {
    assert(sensor_data_it_ != sensor_data_.end());
    time_point          real_now(std::chrono::duration<long long, std::nano>{dataset_now_ - dataset_first_time_});
    const sensor_types& sensor_datum = sensor_data_it_->second;

    imu_.put(imu_.allocate<imu_type>(imu_type{real_now, (sensor_datum.imu0.angular_v), (sensor_datum.imu0.linear_a)}));
    ++sensor_data_it_;
}

PLUGIN_MAIN(offline_imu)
