#include "data_loading.hpp"
#include "illixr/data_format.hpp"
#include "illixr/managed_thread.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/relative_clock.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"

#include <chrono>

using namespace ILLIXR;

class offline_imu : public ILLIXR::threadloop {
public:
    [[maybe_unused]] offline_imu(const std::string& name, phonebook* pb)
        : threadloop{name, pb}
        , sensor_data_{load_data()}
        , sensor_data_it_{sensor_data_.cbegin()}
        , switchboard_{phonebook_->lookup_impl<switchboard>()}
        , imu_{switchboard_->get_writer<imu_type>("imu")}
        , dataset_first_time_{sensor_data_it_->first}
        , dataset_now_{0}
        , imu_cam_log_{record_logger_}
        , clock_{phonebook_->lookup_impl<relative_clock>()} { }

protected:
    skip_option _p_should_skip() override {
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

    void _p_one_iteration() override {
        assert(sensor_data_it_ != sensor_data_.end());
        time_point          real_now(std::chrono::duration<long, std::nano>{dataset_now_ - dataset_first_time_});
        const sensor_types& sensor_datum = sensor_data_it_->second;

        imu_.put(imu_.allocate<imu_type>(imu_type{real_now, (sensor_datum.imu0.angular_v), (sensor_datum.imu0.linear_a)}));
        ++sensor_data_it_;
    }

private:
    const std::map<ullong, sensor_types>           sensor_data_;
    std::map<ullong, sensor_types>::const_iterator sensor_data_it_;
    const std::shared_ptr<switchboard>             switchboard_;
    switchboard::writer<imu_type>                  imu_;

    // Timestamp of the first IMU value from the dataset
    ullong dataset_first_time_;
    // Current IMU timestamp
    ullong dataset_now_;

    record_coalescer imu_cam_log_;

    std::shared_ptr<relative_clock> clock_;
};

PLUGIN_MAIN(offline_imu)
