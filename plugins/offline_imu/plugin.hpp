#pragma once

#include "illixr/data_format/imu.hpp"
#include "illixr/data_format/misc.hpp"
#ifndef __ANDROID__
#    include "illixr/managed_thread.hpp"
#endif
#include "illixr/phonebook.hpp"
#include "illixr/relative_clock.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"

#include <map>

namespace ILLIXR {

struct raw_imu_type {
    Eigen::Vector3d angular_v;
    Eigen::Vector3d linear_a;
};

struct sensor_types {
    raw_imu_type imu0;
};

class MY_EXPORT_API offline_imu : public threadloop {
public:
    [[maybe_unused]] offline_imu(const std::string& name, phonebook* pb);

protected:
    skip_option _p_should_skip() override;
    void        _p_one_iteration() override;

private:
    const std::shared_ptr<switchboard>             switchboard_;
    const std::map<ullong, sensor_types>           sensor_data_;
    std::map<ullong, sensor_types>::const_iterator sensor_data_it_;
    switchboard::writer<data_format::imu_type>     imu_;

    // Timestamp of the first IMU value from the dataset
    ullong dataset_first_time_;
    // Current IMU timestamp
    ullong dataset_now_;
#ifndef __ANDROID__
    record_coalescer imu_cam_log_;
#endif

    std::shared_ptr<relative_clock> clock_;
};
} // namespace ILLIXR
