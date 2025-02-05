#pragma once

#include "illixr/data_format.hpp"
#include "illixr/managed_thread.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/relative_clock.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"

#include <map>

namespace ILLIXR {

typedef struct {
    Eigen::Vector3d angular_v;
    Eigen::Vector3d linear_a;
} raw_imu_type;

typedef struct {
    raw_imu_type imu0;
} sensor_types;

class offline_imu : public threadloop {
public:
    [[maybe_unused]] offline_imu(const std::string& name, phonebook* pb);

protected:
    skip_option _p_should_skip() override;
    void        _p_one_iteration() override;

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
} // namespace ILLIXR
