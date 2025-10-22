#pragma once

#include "illixr/data_format/imu.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/plugin.hpp"
#include "illixr/switchboard.hpp"

namespace ILLIXR {
class MY_EXPORT_API passthrough_integrator : public plugin {
public:
    [[maybe_unused]] passthrough_integrator(const std::string& name, phonebook* pb);

    void callback(const switchboard::ptr<const data_format::imu_type>& datum);

private:
    const std::shared_ptr<switchboard> switchboard_;

    // IMU Data, Sequence Flag, and State Vars Needed
    switchboard::reader<data_format::imu_integrator_input> imu_integrator_input_;

    // IMU state
    switchboard::writer<data_format::imu_raw_type> imu_raw_;
};

} // namespace ILLIXR
