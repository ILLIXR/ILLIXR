#pragma once

#include "illixr/data_format/imu.hpp"
#include "illixr/error_util.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/plugin.hpp"
#include "illixr/relative_clock.hpp"
#include "illixr/switchboard.hpp"

namespace ILLIXR {
class MY_EXPORT_API rk4_integrator : public plugin {
public:
    [[maybe_unused]] rk4_integrator(const std::string& name, phonebook* pb);
    void callback(const switchboard::ptr<const data_format::imu_type>& datum);

private:
    void                                      clean_imu_vec(time_point timestamp);
    void                                      propagate_imu_values(time_point real_time);
    static std::vector<data_format::imu_type> select_imu_readings(const std::vector<data_format::imu_type>& imu_data,
                                                                  time_point time_begin, time_point time_end);
    static data_format::imu_type interpolate_imu(const data_format::imu_type& imu1, const data_format::imu_type& imu2,
                                                 time_point timestamp);

    const std::shared_ptr<switchboard> switchboard_;

    // IMU Data, Sequence Flag, and State Vars Needed
    switchboard::reader<data_format::imu_integrator_input> imu_integrator_input_;

    // IMU Biases
    switchboard::writer<data_format::imu_raw_type> imu_raw_;
    std::vector<data_format::imu_type>             imu_vec_;
    duration                                       last_imu_offset_{};
    bool                                           has_last_offset_ = false;

    [[maybe_unused]] int    counter_       = 0;
    [[maybe_unused]] int    cam_count_     = 0;
    [[maybe_unused]] int    total_imu_     = 0;
    [[maybe_unused]] double last_cam_time_ = 0;
};

} // namespace ILLIXR
