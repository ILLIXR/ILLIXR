#include "common/plugin.hpp"
#include "common/switchboard.hpp"
#include "common/data_format.hpp"
#include "common/phonebook.hpp"

#include <ecal/ecal.h>
#include <ecal/msg/protobuf/publisher.h>
#include <google/protobuf/util/time_util.h>

#include <thread>
#include <string>

#include "vio_output.pb.h"

using namespace ILLIXR;

class server_writer : public plugin {
public:
    server_writer(std::string name_, phonebook* pb_)
		: plugin{name_, pb_}
		, sb{pb->lookup_impl<switchboard>()}
		, _m_imu_int_input{sb->get_reader<imu_integrator_input>("imu_integrator_input")}
    { 
		// Initialize eCAL and create a protobuf publisher
		eCAL::Initialize(0, NULL, "VIO Offloading Sensor Data Writer");
		publisher = eCAL::protobuf::CPublisher
		<vio_output_proto::VIOOutput>("vio_output");
	}


    virtual void start() override {
        plugin::start();

        sb->schedule<pose_type>(id, "slow_pose", [this](switchboard::ptr<const pose_type> datum, std::size_t) {
			this->send_vio_output(datum);
		});
	}


    void send_vio_output(switchboard::ptr<const pose_type> datum) {

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
		protobuf_imu_int_input->set_t_offset(imu_int_input->t_offset);
		protobuf_imu_int_input->set_last_cam_integration_time(imu_int_input->last_cam_integration_time);

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

		publisher.Send(*vio_output_params);
		delete vio_output_params;
    }

private:
    const std::shared_ptr<switchboard> sb;
	switchboard::reader<imu_integrator_input> _m_imu_int_input;
	eCAL::protobuf::CPublisher<vio_output_proto::VIOOutput> publisher;
};

PLUGIN_MAIN(server_writer)