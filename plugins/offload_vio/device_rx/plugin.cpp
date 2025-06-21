#include "plugin.hpp"

#include "illixr/network/net_config.hpp"

#include <utility>

using namespace ILLIXR;
using namespace ILLIXR::data_format;

[[maybe_unused]] offload_reader::offload_reader(const std::string& name, phonebook* pb)
    : threadloop{name, pb}
    , switchboard_{phonebook_->lookup_impl<switchboard>()}
    , clock_{phonebook_->lookup_impl<relative_clock>()}
    , vio_pose_reader_{switchboard_->get_buffered_reader<switchboard::event_wrapper<std::string>>("vio_pose")}
    , pose_{switchboard_->get_writer<pose_type>("slow_pose")}
    , imu_integrator_input_{switchboard_->get_writer<imu_integrator_input>("imu_integrator_input")} {
    spdlogger(switchboard_->get_env_char("OFFLOAD_VIO_LOG_LEVEL"));
    pose_type                   datum_pose_tmp{time_point{}, time_point{}, Eigen::Vector3f{0, 0, 0}, Eigen::Quaternionf{1, 0, 0, 0}};
    switchboard::ptr<pose_type> datum_pose = pose_.allocate<pose_type>(std::move(datum_pose_tmp));
    pose_.put(std::move(datum_pose));
}

ILLIXR::threadloop::skip_option offload_reader::_p_should_skip() {
    return skip_option::run;
}

void offload_reader::_p_one_iteration() {
    if (vio_pose_reader_.size() > 0) {
        auto                   buffer_ptr   = vio_pose_reader_.dequeue();
        std::string            buffer_str   = **buffer_ptr;
        std::string::size_type end_position = buffer_str.find(delimiter);
        // process the data
        vio_output_proto::VIOOutput vio_output;
        bool                        success = vio_output.ParseFromString(buffer_str.substr(0, end_position));
        if (success) {
            receive_vio_output(vio_output);
        } else {
            spdlog::get(name_)->error("[offload_vio.device_rx: Cannot parse VIO output!!");
        }
    }
}

void offload_reader::receive_vio_output(const vio_output_proto::VIOOutput& vio_output) {
    const vio_output_proto::SlowPose& slow_pose = vio_output.slow_pose();

    pose_type datum_pose_tmp{
        time_point{std::chrono::nanoseconds{slow_pose.cam_timestamp()}},
        time_point{std::chrono::nanoseconds{slow_pose.imu_timestamp()}},
        Eigen::Vector3f{static_cast<float>(slow_pose.position().x()), static_cast<float>(slow_pose.position().y()),
                        static_cast<float>(slow_pose.position().z())},
        Eigen::Quaternionf{static_cast<float>(slow_pose.rotation().w()), static_cast<float>(slow_pose.rotation().x()),
                           static_cast<float>(slow_pose.rotation().y()), static_cast<float>(slow_pose.rotation().z())}};

    switchboard::ptr<pose_type> datum_pose = pose_.allocate<pose_type>(std::move(datum_pose_tmp));
    pose_.put(std::move(datum_pose));

    const vio_output_proto::IMUIntInput& imu_int_input = vio_output.imu_int_input();

    imu_integrator_input datum_imu_int_tmp{
        time_point{std::chrono::nanoseconds{imu_int_input.last_cam_integration_time()}},
        duration(std::chrono::nanoseconds{imu_int_input.t_offset()}),
        imu_params{
            imu_int_input.imu_params().gyro_noise(),
            imu_int_input.imu_params().acc_noise(),
            imu_int_input.imu_params().gyro_walk(),
            imu_int_input.imu_params().acc_walk(),
            Eigen::Matrix<double, 3, 1>{
                imu_int_input.imu_params().n_gravity().x(),
                imu_int_input.imu_params().n_gravity().y(),
                imu_int_input.imu_params().n_gravity().z(),
            },
            imu_int_input.imu_params().imu_integration_sigma(),
            imu_int_input.imu_params().nominal_rate(),
        },
        Eigen::Vector3d{imu_int_input.biasacc().x(), imu_int_input.biasacc().y(), imu_int_input.biasacc().z()},
        Eigen::Vector3d{imu_int_input.biasgyro().x(), imu_int_input.biasgyro().y(), imu_int_input.biasgyro().z()},
        Eigen::Matrix<double, 3, 1>{imu_int_input.position().x(), imu_int_input.position().y(), imu_int_input.position().z()},
        Eigen::Matrix<double, 3, 1>{imu_int_input.velocity().x(), imu_int_input.velocity().y(), imu_int_input.velocity().z()},
        Eigen::Quaterniond{imu_int_input.rotation().w(), imu_int_input.rotation().x(), imu_int_input.rotation().y(),
                           imu_int_input.rotation().z()}};

    switchboard::ptr<imu_integrator_input> datum_imu_int =
        imu_integrator_input_.allocate<imu_integrator_input>(std::move(datum_imu_int_tmp));
    imu_integrator_input_.put(std::move(datum_imu_int));
}

PLUGIN_MAIN(offload_reader)
