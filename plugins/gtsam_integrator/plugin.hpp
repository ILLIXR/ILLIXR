#pragma once
#include "illixr/data_format/imu.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/plugin.hpp"
#include "illixr/switchboard.hpp"
#include "third_party/filter.h"

#include <gtsam/navigation/CombinedImuFactor.h> // Used if IMU combined is off.
#include <gtsam/navigation/ImuFactor.h>

using ImuBias = gtsam::imuBias::ConstantBias;

using imu_int_t = ILLIXR::data_format::imu_integrator_input;
using imu_t     = ILLIXR::data_format::imu_type;
using bias_t    = ImuBias;
using nav_t     = gtsam::NavState;
using pim_t     = gtsam::PreintegratedCombinedMeasurements;
using pim_ptr_t = gtsam::PreintegrationType*;

namespace ILLIXR {
class gtsam_integrator : public plugin {
public:
    [[maybe_unused]] gtsam_integrator(const std::string& name, phonebook* pb);
    void callback(const switchboard::ptr<const data_format::imu_type>& datum);

private:
    /**
     * @brief Wrapper object protecting the lifetime of IMU integration inputs and biases
     */
    class pim_object {
    public:
        explicit pim_object(const imu_int_t& imu_int_input);
        ~pim_object();
        void                 reset_integration_and_set_bias(const imu_int_t& imu_int_input) noexcept;
        void                 integrate_measurement(const imu_t& imu_input, const imu_t& imu_input_next) noexcept;
        [[nodiscard]] bias_t bias_hat() const noexcept;
        [[nodiscard]] nav_t  predict() const noexcept;

    private:
        bias_t    imu_bias_;
        nav_t     nav_state_lkf_;
        pim_ptr_t pim_;
    };

    void                                      clean_imu_vec(time_point timestamp);
    void                                      propagate_imu_values(time_point real_time);
    static std::vector<data_format::imu_type> select_imu_readings(const std::vector<data_format::imu_type>& imu_data,
                                                                  const time_point time_begin, const time_point time_end);
    static data_format::imu_type interpolate_imu(const data_format::imu_type& imu_1, const data_format::imu_type& imu_2,
                                                 time_point timestamp);

    const std::shared_ptr<switchboard>    switchboard_;
    const std::shared_ptr<relative_clock> clock_;

    // IMU Data, Sequence Flag, and State Vars Needed
    switchboard::reader<data_format::imu_integrator_input> imu_integrator_input_;

    // Write IMU Biases for PP
    switchboard::writer<data_format::imu_raw_type> imu_raw_;

    std::shared_ptr<spdlog::logger> log_;

    std::vector<one_euro_filter<Eigen::Array<double, 3, 1>, double>> filters_;
    bool                                                             has_prev_ = false;
    Eigen::Matrix<double, 3, 1>                                      prev_euler_angles_;
    std::vector<data_format::imu_type>                               imu_vector_;

    // std::vector<pose_type> filtered_poses;

    [[maybe_unused]] time_point last_cam_time_{};
    duration                    last_imu_offset_{};

    std::unique_ptr<pim_object> pim_obj_;
};
} // namespace ILLIXR
