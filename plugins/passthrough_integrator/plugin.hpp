#pragma once

#include "illixr/plugin.hpp"

#include "illixr/data_format.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/switchboard.hpp"

namespace ILLIXR {
class passthrough_integrator : public plugin {
public:
    [[maybe_unused]]passthrough_integrator(const std::string& name, phonebook* pb);

    void callback(const switchboard::ptr<const imu_type>& datum);
private:
    const std::shared_ptr<switchboard> switchboard_;

    // IMU Data, Sequence Flag, and State Vars Needed
    switchboard::reader<imu_integrator_input> imu_integrator_input_;

    // IMU state
    switchboard::writer<imu_raw_type> imu_raw_;
};

}