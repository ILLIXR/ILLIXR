#include "illixr/data_format.hpp"
#include "illixr/network/net_config.hpp"
#include "illixr/network/socket.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"
#include "vio_output.pb.h"

#include <utility>

using namespace ILLIXR;

class offload_reader : public threadloop {
public:
    offload_reader(const std::string& name_, phonebook* pb_)
        : threadloop{name_, pb_}
        , sb{pb->lookup_impl<switchboard>()}
        , _m_clock{pb->lookup_impl<RelativeClock>()}
        , _m_pose{sb->get_writer<pose_type>("slow_pose")}
        , _m_imu_integrator_input{sb->get_writer<imu_integrator_input>("imu_integrator_input")}
        , server_addr(SERVER_IP, SERVER_PORT_2) {
        spdlogger(std::getenv("OFFLOAD_VIO_LOG_LEVEL"));
        pose_type                   datum_pose_tmp{time_point{}, Eigen::Vector3f{0, 0, 0}, Eigen::Quaternionf{1, 0, 0, 0}};
        switchboard::ptr<pose_type> datum_pose = _m_pose.allocate<pose_type>(std::move(datum_pose_tmp));
        _m_pose.put(std::move(datum_pose));

        socket.set_reuseaddr();
        socket.bind(Address(CLIENT_IP, CLIENT_PORT_2));
        socket.enable_no_delay();
        is_socket_connected = false;
    }

    skip_option _p_should_skip() override {
        if (!is_socket_connected) {
#ifndef NDEBUG
            spdlog::get(name)->debug("[offload_vio.device_rx]: Connecting to {}", server_addr.str(":"));
#endif
            socket.connect(server_addr);
#ifndef NDEBUG
            spdlog::get(name)->debug("[offload_vio.device_rx]: Connected to {}", server_addr.str(":"));
#endif
            is_socket_connected = true;
        }
        return skip_option::run;
    }

    void _p_one_iteration() override {
        if (is_socket_connected) {
            auto        now        = timestamp();
            std::string delimitter = "END!";
            std::string recv_data  = socket.read(); /* Blocking operation, wait for the data to come */
            if (!recv_data.empty()) {
                buffer_str                          = buffer_str + recv_data;
                std::string::size_type end_position = buffer_str.find(delimitter);
                while (end_position != std::string::npos) {
                    std::string before = buffer_str.substr(0, end_position);
                    buffer_str         = buffer_str.substr(end_position + delimitter.size());

                    // process the data
                    vio_output_proto::VIOOutput vio_output;
                    bool                        success = vio_output.ParseFromString(before);
                    if (success) {
                        ReceiveVioOutput(vio_output, before);
                    } else {
                        spdlog::get(name)->error("[offload_vio.device_rx: Cannot parse VIO output!!");
                    }
                    end_position = buffer_str.find(delimitter);
                }
            }
        }
    }

private:
    void ReceiveVioOutput(const vio_output_proto::VIOOutput& vio_output, const std::string& str_data) {
        const vio_output_proto::SlowPose& slow_pose = vio_output.slow_pose();

        pose_type datum_pose_tmp{
            time_point{std::chrono::nanoseconds{slow_pose.timestamp()}},
            Eigen::Vector3f{static_cast<float>(slow_pose.position().x()), static_cast<float>(slow_pose.position().y()),
                            static_cast<float>(slow_pose.position().z())},
            Eigen::Quaternionf{static_cast<float>(slow_pose.rotation().w()), static_cast<float>(slow_pose.rotation().x()),
                               static_cast<float>(slow_pose.rotation().y()), static_cast<float>(slow_pose.rotation().z())}};

        switchboard::ptr<pose_type> datum_pose = _m_pose.allocate<pose_type>(std::move(datum_pose_tmp));
        _m_pose.put(std::move(datum_pose));

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
            Eigen::Matrix<double, 3, 1>{imu_int_input.position().x(), imu_int_input.position().y(),
                                        imu_int_input.position().z()},
            Eigen::Matrix<double, 3, 1>{imu_int_input.velocity().x(), imu_int_input.velocity().y(),
                                        imu_int_input.velocity().z()},
            Eigen::Quaterniond{imu_int_input.rotation().w(), imu_int_input.rotation().x(), imu_int_input.rotation().y(),
                               imu_int_input.rotation().z()}};

        switchboard::ptr<imu_integrator_input> datum_imu_int =
            _m_imu_integrator_input.allocate<imu_integrator_input>(std::move(datum_imu_int_tmp));
        _m_imu_integrator_input.put(std::move(datum_imu_int));
    }

    const std::shared_ptr<switchboard>        sb;
    const std::shared_ptr<RelativeClock>      _m_clock;
    switchboard::writer<pose_type>            _m_pose;
    switchboard::writer<imu_integrator_input> _m_imu_integrator_input;

    TCPSocket   socket;
    bool        is_socket_connected;
    Address     server_addr;
    std::string buffer_str;
};

PLUGIN_MAIN(offload_reader)
