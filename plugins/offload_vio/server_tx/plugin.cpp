#include "illixr/plugin.hpp"

#include "illixr/data_format.hpp"
#include "illixr/network/net_config.hpp"
#include "illixr/network/socket.hpp"
#include "illixr/network/timestamp.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/switchboard.hpp"
#include "vio_output.pb.h"

#include <memory>
#include <string>
#include <utility>

using namespace ILLIXR;

class server_writer : public plugin {
public:
    server_writer(std::string name_, phonebook* pb_)
        : plugin{std::move(name_), pb_}
        , sb{pb->lookup_impl<switchboard>()}
        , _m_imu_int_input{sb->get_reader<imu_integrator_input>("imu_integrator_input")}
        , client_addr(CLIENT_IP, CLIENT_PORT_2) {
        spdlogger(std::getenv("OFFLOAD_VIO_LOG_LEVEL"));
        socket.set_reuseaddr();
        socket.bind(Address(SERVER_IP, SERVER_PORT_2));
        socket.enable_no_delay();
        is_client_connected = false;
    }

    // This schedule function cant go in the constructor because there seems to be an issue with
    // the callbeing being triggered before any data is written to slow_pose. This needs debugging.
    void start() override {
        plugin::start();

        sb->schedule<pose_type>(id, "slow_pose", [this](const switchboard::ptr<const pose_type>& datum, std::size_t) {
            this->send_vio_output(datum);
        });
        sb->schedule<connection_signal>(id, "connection_signal",
                                        [this](switchboard::ptr<const connection_signal> datum, std::size_t) {
                                            this->start_accepting_connection(datum);
                                        });
    }

    void start_accepting_connection(switchboard::ptr<const connection_signal> datum) {
        socket.listen();
#ifndef NDEBUG
        spdlog::get(name)->debug("[offload_vio.server_tx]: Waiting for connection!");
#endif
        write_socket = new TCPSocket(FileDescriptor(system_call(
            "accept", ::accept(socket.fd_num(), nullptr, nullptr)))); /* Blocking operation, waiting for client to connect */
#ifndef NDEBUG
        spdlog::get(name)->debug("[offload_vio.server_tx]: Connection is established with {}",
                                 write_socket->peer_address().str(":"));
#endif
    }

    void send_vio_output(const switchboard::ptr<const pose_type>& datum) {
        // Check if a socket connection has been established
        if (write_socket != NULL) {
            // Construct slow pose for output
            vio_output_proto::SlowPose* protobuf_slow_pose = new vio_output_proto::SlowPose();
            protobuf_slow_pose->set_timestamp(datum->sensor_time.time_since_epoch().count());

            vio_output_proto::Vec3* position = new vio_output_proto::Vec3();
            position->set_x(datum->position.x());
            position->set_y(datum->position.y());
            position->set_z(datum->position.z());
            protobuf_slow_pose->set_allocated_position(position);

            vio_output_proto::Quat* rotation = new vio_output_proto::Quat();
            rotation->set_w(datum->orientation.w());
            rotation->set_x(datum->orientation.x());
            rotation->set_y(datum->orientation.y());
            rotation->set_z(datum->orientation.z());
            protobuf_slow_pose->set_allocated_rotation(rotation);

            // Construct IMU integrator input for output
            switchboard::ptr<const imu_integrator_input> imu_int_input = _m_imu_int_input.get_ro_nullable();

            vio_output_proto::IMUIntInput* protobuf_imu_int_input = new vio_output_proto::IMUIntInput();
            protobuf_imu_int_input->set_t_offset(imu_int_input->t_offset.count());
            protobuf_imu_int_input->set_last_cam_integration_time(
                imu_int_input->last_cam_integration_time.time_since_epoch().count());

            vio_output_proto::IMUParams* imu_params = new vio_output_proto::IMUParams();
            imu_params->set_gyro_noise(imu_int_input->params.gyro_noise);
            imu_params->set_acc_noise(imu_int_input->params.acc_noise);
            imu_params->set_gyro_walk(imu_int_input->params.gyro_walk);
            imu_params->set_acc_walk(imu_int_input->params.acc_walk);
            vio_output_proto::Vec3* n_gravity = new vio_output_proto::Vec3();
            n_gravity->set_x(imu_int_input->params.n_gravity.x());
            n_gravity->set_y(imu_int_input->params.n_gravity.y());
            n_gravity->set_z(imu_int_input->params.n_gravity.z());
            imu_params->set_allocated_n_gravity(n_gravity);
            imu_params->set_imu_integration_sigma(imu_int_input->params.imu_integration_sigma);
            imu_params->set_nominal_rate(imu_int_input->params.nominal_rate);
            protobuf_imu_int_input->set_allocated_imu_params(imu_params);

            vio_output_proto::Vec3* biasAcc = new vio_output_proto::Vec3();
            biasAcc->set_x(imu_int_input->biasAcc.x());
            biasAcc->set_y(imu_int_input->biasAcc.y());
            biasAcc->set_z(imu_int_input->biasAcc.z());
            protobuf_imu_int_input->set_allocated_biasacc(biasAcc);

            vio_output_proto::Vec3* biasGyro = new vio_output_proto::Vec3();
            biasGyro->set_x(imu_int_input->biasGyro.x());
            biasGyro->set_y(imu_int_input->biasGyro.y());
            biasGyro->set_z(imu_int_input->biasGyro.z());
            protobuf_imu_int_input->set_allocated_biasgyro(biasGyro);

            vio_output_proto::Vec3* position_int = new vio_output_proto::Vec3();
            position_int->set_x(imu_int_input->position.x());
            position_int->set_y(imu_int_input->position.y());
            position_int->set_z(imu_int_input->position.z());
            protobuf_imu_int_input->set_allocated_position(position_int);

            vio_output_proto::Vec3* velocity = new vio_output_proto::Vec3();
            velocity->set_x(imu_int_input->velocity.x());
            velocity->set_y(imu_int_input->velocity.y());
            velocity->set_z(imu_int_input->velocity.z());
            protobuf_imu_int_input->set_allocated_velocity(velocity);

            vio_output_proto::Quat* rotation_int = new vio_output_proto::Quat();
            rotation_int->set_w(imu_int_input->quat.w());
            rotation_int->set_x(imu_int_input->quat.x());
            rotation_int->set_y(imu_int_input->quat.y());
            rotation_int->set_z(imu_int_input->quat.z());
            protobuf_imu_int_input->set_allocated_rotation(rotation_int);

            vio_output_proto::VIOOutput* vio_output_params = new vio_output_proto::VIOOutput();
            vio_output_params->set_allocated_slow_pose(protobuf_slow_pose);
            vio_output_params->set_allocated_imu_int_input(protobuf_imu_int_input);

            unsigned long long end_pose_time =
                std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch())
                    .count();
            vio_output_params->set_end_server_timestamp(end_pose_time);

            // Prepare data delivery
            std::string data_to_be_sent = vio_output_params->SerializeAsString();
            std::string delimitter      = "END!";

            write_socket->write(data_to_be_sent + delimitter);

            delete vio_output_params;
        } else {
            spdlog::get(name)->error("[offload_vio.server_tx] ERROR: write_socket is not yet created!");
        }
    }

private:
    const std::shared_ptr<switchboard>        sb;
    switchboard::reader<imu_integrator_input> _m_imu_int_input;

    TCPSocket  socket;
    TCPSocket* write_socket = NULL;
    Address    client_addr;
    bool       is_client_connected;
};

PLUGIN_MAIN(server_writer)