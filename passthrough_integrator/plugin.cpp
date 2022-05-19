#include "common/plugin.hpp"
#include "common/phonebook.hpp"
#include "common/switchboard.hpp"
#include "common/data_format.hpp"

using namespace ILLIXR;

class passthrough_integrator : public plugin {
public:
	passthrough_integrator(std::string name_, phonebook* pb_)
		: plugin{name_, pb_}
		, sb{pb->lookup_impl<switchboard>()}
		, _m_imu_integrator_input{sb->get_reader<imu_integrator_input>("imu_integrator_input")}
		, _m_imu_raw{sb->get_writer<imu_raw_type>("imu_raw")}
	{
		sb->schedule<imu_type>(id, "imu", [&](switchboard::ptr<const imu_type> datum, size_t) {
			callback(datum);
		});
	}

	void callback(switchboard::ptr<const imu_type> datum) {
		auto input_values = _m_imu_integrator_input.get_ro_nullable();
		if (input_values == nullptr) {
			return;
		}

		Eigen::Matrix<double,4,1> curr_quat {
			input_values->quat.x(), 
			input_values->quat.y(),
			input_values->quat.z(),
			input_values->quat.w()
		};
		Eigen::Matrix<double,3,1> curr_pos = input_values->position;
		Eigen::Matrix<double,3,1> curr_vel = input_values->velocity;

		Eigen::Matrix<double,3,1> w_hat;
		Eigen::Matrix<double,3,1> a_hat;
		Eigen::Matrix<double,3,1> w_hat2;
		Eigen::Matrix<double,3,1> a_hat2;

		w_hat = datum->angular_v.cast<double>() - input_values->biasGyro;
		a_hat = datum->linear_a.cast<double>() - input_values->biasAcc;
		w_hat2 = datum->angular_v.cast<double>() - input_values->biasGyro;
		a_hat2 = datum->linear_a.cast<double>() - input_values->biasAcc;

		_m_imu_raw.put(_m_imu_raw.allocate(
			w_hat,
			a_hat,
			w_hat2,
			a_hat2,
			curr_pos,
			curr_vel,
			Eigen::Quaterniond{curr_quat(3), curr_quat(0), curr_quat(1), curr_quat(2)},
			datum->time
		));
	}

private:
	const std::shared_ptr<switchboard> sb;

	// IMU Data, Sequence Flag, and State Vars Needed
	switchboard::reader<imu_integrator_input> _m_imu_integrator_input;

	// IMU state
	switchboard::writer<imu_raw_type> _m_imu_raw;
};

PLUGIN_MAIN(passthrough_integrator)
