#include "common/plugin.hpp"
#include "common/switchboard.hpp"
#include "common/data_format.hpp"
#include "common/phonebook.hpp"

#include <ecal/ecal.h>
#include <ecal/msg/protobuf/subscriber.h>
#include <google/protobuf/util/time_util.h>

#include "vio_output.pb.h"

using namespace ILLIXR;

class offload_reader : public plugin {
public:
    offload_reader(std::string name_, phonebook* pb_)
		: plugin{name_, pb_}
		, sb{pb->lookup_impl<switchboard>()}
		, _m_pose{sb->get_writer<pose_type>("slow_pose")}
		, _m_imu_integrator_input{sb->get_writer<imu_integrator_input>("imu_integrator_input")}
    { 
		pose_type datum_pose_tmp{
            ILLIXR::time_type{},
            Eigen::Vector3f{0, 0, 0},
            Eigen::Quaternionf{1, 0, 0, 0}
        };
        switchboard::ptr<pose_type> datum_pose = _m_pose.allocate<pose_type>(std::move(datum_pose_tmp));
        _m_pose.put(std::move(datum_pose));

		eCAL::Initialize(0, NULL, "VIO Offloading Sensor Data Reader");
		subscriber_slow_pose = eCAL::protobuf::CSubscriber
		<vio_output_proto::SlowPose>("output_slow_pose");
		subscriber_imu_int = eCAL::protobuf::CSubscriber
		<vio_output_proto::IMUIntInput>("output_imu_int");
	}

	virtual void _p_one_iteration() {
		vio_output_proto::SlowPose slow_pose;
		subscriber_slow_pose.Receive(slow_pose);

		pose_type datum_pose_tmp{
			ILLIXR::time_type{std::chrono::nanoseconds{slow_pose.timestamp()}},
			Eigen::Vector3f{slow_pose.position().x(), slow_pose.position().y(), slow_pose.position().z()},
			Eigen::Quaternionf{slow_pose.rotation().w(), slow_pose.rotation().x(), slow_pose.rotation().y(), slow_pose.rotation().z()}
		};

		switchboard::ptr<pose_type> datum_pose = _m_pose.allocate<pose_type>(std::move(datum_pose_tmp));
        _m_pose.put(std::move(datum_pose));

		vio_output_proto::IMUIntInput imu_int_input;
		subscriber_imu_int.Receive(imu_int_input);

		imu_integrator_input datum_imu_int_tmp{
			static_cast<double>(slow_pose.timestamp()),
			imu_int_input.t_offset(),
			imu_params{
				imu_int_input.imu_params().gyro_noise(),
				imu_int_input.imu_params().acc_noise(),
				imu_int_input.imu_params().gyro_walk(),
				imu_int_input.imu_params().acc_walk(),
				Eigen::Matrix<double,3,1>{
					imu_int_input.imu_params().n_gravity().x(),
					imu_int_input.imu_params().n_gravity().y(),
					imu_int_input.imu_params().n_gravity().z(),
				},
				imu_int_input.imu_params().imu_integration_sigma(),
				imu_int_input.imu_params().nominal_rate(),
			},
			Eigen::Vector3d{imu_int_input.biasacc().x(), imu_int_input.biasacc().y(), imu_int_input.biasacc().z()},
			Eigen::Vector3d{imu_int_input.biasgyro().x(), imu_int_input.biasgyro().y(), imu_int_input.biasgyro().z()},
			Eigen::Matrix<double,3,1>{slow_pose.position().x(), slow_pose.position().y(), slow_pose.position().z()},
			Eigen::Matrix<double,3,1>{imu_int_input.velocity().x(), imu_int_input.velocity().y(), imu_int_input.velocity().z()},
			Eigen::Quaterniond{slow_pose.rotation().w(), slow_pose.rotation().x(), slow_pose.rotation().y(), slow_pose.rotation().z()}
		};

		switchboard::ptr<imu_integrator_input> datum_imu_int =
            _m_imu_integrator_input.allocate<imu_integrator_input>(std::move(datum_imu_int_tmp));
        _m_imu_integrator_input.put(std::move(datum_imu_int));
	}


private:
    const std::shared_ptr<switchboard> sb;
	switchboard::writer<pose_type> _m_pose;
	switchboard::writer<imu_integrator_input> _m_imu_integrator_input;

	eCAL::protobuf::CSubscriber<vio_output_proto::SlowPose> subscriber_slow_pose;
	eCAL::protobuf::CSubscriber<vio_output_proto::IMUIntInput> subscriber_imu_int;
};

PLUGIN_MAIN(offload_reader)