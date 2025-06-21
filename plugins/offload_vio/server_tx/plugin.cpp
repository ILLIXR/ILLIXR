#include "plugin.hpp"

#include "illixr/network/net_config.hpp"
#include "illixr/network/tcpsocket.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/switchboard.hpp"
// if the header exists, we are good; if not generate a stub class for IDEs to reduce on-screen errors
#if __has_include("vio_output.pb.h")
    #include "vio_output.pb.h"
#else
    #include "../proto/output_stub.hpp"
#endif

#include <memory>
#include <string>

using namespace ILLIXR;
using namespace ILLIXR::data_format;

[[maybe_unused]] server_writer::server_writer(const std::string& name, phonebook* pb)
    : plugin{name, pb}
    , switchboard_{phonebook_->lookup_impl<switchboard>()}
    , imu_int_input_{switchboard_->get_reader<imu_integrator_input>("imu_integrator_input")}
    , vio_pose_writer_{switchboard_->get_network_writer<switchboard::event_wrapper<std::string>>(
          "vio_pose", network::topic_config{.serialization_method = network::topic_config::SerializationMethod::PROTOBUF})} {
    spdlogger(switchboard_->get_env_char("OFFLOAD_VIO_LOG_LEVEL"));
}

// This schedule function cant go in the constructor because there seems to be an issue with
// the call being triggered before any data is written to slow_pose. This needs debugging.
void server_writer::start() {
    plugin::start();

    switchboard_->schedule<pose_type>(id_, "slow_pose", [this](const switchboard::ptr<const pose_type>& datum, std::size_t) {
        this->send_vio_output(datum);
    });
}

void server_writer::send_vio_output(const switchboard::ptr<const pose_type>& datum) {
    // Construct slow pose for output
    auto* protobuf_slow_pose = new vio_output_proto::SlowPose();
    protobuf_slow_pose->set_cam_timestamp(datum->cam_time.time_since_epoch().count());
    protobuf_slow_pose->set_imu_timestamp(datum->imu_time.time_since_epoch().count());

    auto* position = new vio_output_proto::Vec3();
    position->set_x(datum->position.x());
    position->set_y(datum->position.y());
    position->set_z(datum->position.z());
    protobuf_slow_pose->set_allocated_position(position);

    auto* rotation = new vio_output_proto::Quat();
    rotation->set_w(datum->orientation.w());
    rotation->set_x(datum->orientation.x());
    rotation->set_y(datum->orientation.y());
    rotation->set_z(datum->orientation.z());
    protobuf_slow_pose->set_allocated_rotation(rotation);

    // Construct IMU integrator input for output
    switchboard::ptr<const imu_integrator_input> imu_int_input = imu_int_input_.get_ro_nullable();

    auto* protobuf_imu_int_input = new vio_output_proto::IMUIntInput();
    protobuf_imu_int_input->set_t_offset(imu_int_input->t_offset.count());
    protobuf_imu_int_input->set_last_cam_integration_time(imu_int_input->last_cam_integration_time.time_since_epoch().count());

    auto* imu_params = new vio_output_proto::IMUParams();
    imu_params->set_gyro_noise(imu_int_input->params.gyro_noise);
    imu_params->set_acc_noise(imu_int_input->params.acc_noise);
    imu_params->set_gyro_walk(imu_int_input->params.gyro_walk);
    imu_params->set_acc_walk(imu_int_input->params.acc_walk);
    auto* n_gravity = new vio_output_proto::Vec3();
    n_gravity->set_x(imu_int_input->params.n_gravity.x());
    n_gravity->set_y(imu_int_input->params.n_gravity.y());
    n_gravity->set_z(imu_int_input->params.n_gravity.z());
    imu_params->set_allocated_n_gravity(n_gravity);
    imu_params->set_imu_integration_sigma(imu_int_input->params.imu_integration_sigma);
    imu_params->set_nominal_rate(imu_int_input->params.nominal_rate);
    protobuf_imu_int_input->set_allocated_imu_params(imu_params);

    auto* bias_acc = new vio_output_proto::Vec3();
    bias_acc->set_x(imu_int_input->bias_acc.x());
    bias_acc->set_y(imu_int_input->bias_acc.y());
    bias_acc->set_z(imu_int_input->bias_acc.z());
    protobuf_imu_int_input->set_allocated_biasacc(bias_acc);

    auto* bias_gyro = new vio_output_proto::Vec3();
    bias_gyro->set_x(imu_int_input->bias_gyro.x());
    bias_gyro->set_y(imu_int_input->bias_gyro.y());
    bias_gyro->set_z(imu_int_input->bias_gyro.z());
    protobuf_imu_int_input->set_allocated_biasgyro(bias_gyro);

    auto* position_int = new vio_output_proto::Vec3();
    position_int->set_x(imu_int_input->position.x());
    position_int->set_y(imu_int_input->position.y());
    position_int->set_z(imu_int_input->position.z());
    protobuf_imu_int_input->set_allocated_position(position_int);

    auto* velocity = new vio_output_proto::Vec3();
    velocity->set_x(imu_int_input->velocity.x());
    velocity->set_y(imu_int_input->velocity.y());
    velocity->set_z(imu_int_input->velocity.z());
    protobuf_imu_int_input->set_allocated_velocity(velocity);

    auto* rotation_int = new vio_output_proto::Quat();
    rotation_int->set_w(imu_int_input->quat.w());
    rotation_int->set_x(imu_int_input->quat.x());
    rotation_int->set_y(imu_int_input->quat.y());
    rotation_int->set_z(imu_int_input->quat.z());
    protobuf_imu_int_input->set_allocated_rotation(rotation_int);

    auto* vio_output_params = new vio_output_proto::VIOOutput();
    vio_output_params->set_allocated_slow_pose(protobuf_slow_pose);
    vio_output_params->set_allocated_imu_int_input(protobuf_imu_int_input);

    unsigned long long end_pose_time =
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    vio_output_params->set_end_server_timestamp(end_pose_time);

    // Prepare data delivery
    std::string       data_to_be_sent = vio_output_params->SerializeAsString();
    const std::string delimiter       = "END!";

    vio_pose_writer_.put(std::make_shared<switchboard::event_wrapper<std::string>>(data_to_be_sent + delimiter));

    delete vio_output_params;
}

PLUGIN_MAIN(server_writer)
