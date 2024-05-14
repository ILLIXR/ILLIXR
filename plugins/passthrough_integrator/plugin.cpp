#include "illixr/plugin.hpp"

#include "illixr/data_format.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/switchboard.hpp"

#include <eigen3/Eigen/Dense>
#include <memory>

using namespace ILLIXR;

class passthrough_integrator : public plugin {
public:
    [[maybe_unused]] passthrough_integrator(const std::string& name, phonebook* pb)
        : plugin{name, pb}
        , switchboard_{phonebook_->lookup_impl<switchboard>()}
        , imu_integrator_input_{switchboard_->get_reader<imu_integrator_input>("imu_integrator_input")}
        , imu_raw_{switchboard_->get_writer<imu_raw_type>("imu_raw")} {
        switchboard_->schedule<imu_type>(id_, "imu", [&](const switchboard::ptr<const imu_type>& datum, size_t) {
            callback(datum);
        });
    }

    void callback(const switchboard::ptr<const imu_type>& datum) {
        auto input_values = imu_integrator_input_.get_ro_nullable();
        if (input_values == nullptr) {
            return;
        }

        Eigen::Matrix<double, 4, 1> curr_quat{input_values->quat.x(), input_values->quat.y(), input_values->quat.z(),
                                              input_values->quat.w()};
        Eigen::Matrix<double, 3, 1> curr_pos = input_values->position;
        Eigen::Matrix<double, 3, 1> curr_vel = input_values->velocity;

        Eigen::Matrix<double, 3, 1> w_hat;
        Eigen::Matrix<double, 3, 1> a_hat;
        Eigen::Matrix<double, 3, 1> w_hat2;
        Eigen::Matrix<double, 3, 1> a_hat2;

        w_hat  = datum->angular_v - input_values->bias_gyro;
        a_hat  = datum->linear_a - input_values->bias_acc;
        w_hat2 = datum->angular_v - input_values->bias_gyro;
        a_hat2 = datum->linear_a - input_values->bias_acc;

        imu_raw_.put(imu_raw_.allocate(w_hat, a_hat, w_hat2, a_hat2, curr_pos, curr_vel,
                                       Eigen::Quaterniond{curr_quat(3), curr_quat(0), curr_quat(1), curr_quat(2)},
                                       datum->time));
    }

private:
    const std::shared_ptr<switchboard> switchboard_;

    // IMU Data, Sequence Flag, and State Vars Needed
    switchboard::reader<imu_integrator_input> imu_integrator_input_;

    // IMU state
    switchboard::writer<imu_raw_type> imu_raw_;
};

PLUGIN_MAIN(passthrough_integrator)
