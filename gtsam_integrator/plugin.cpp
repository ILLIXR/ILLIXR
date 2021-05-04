#include <chrono>
#include <iomanip>
#include <thread>
#include <eigen3/Eigen/Dense>

#include "common/plugin.hpp"
#include "common/switchboard.hpp"
#include "common/data_format.hpp"
#include "common/threadloop.hpp"

#include <gtsam/base/Matrix.h>
#include <gtsam/base/Vector.h>
#include <gtsam/navigation/AHRSFactor.h>
#include <gtsam/navigation/CombinedImuFactor.h>  // Used if IMU combined is off.
#include <gtsam/navigation/ImuBias.h>
#include <gtsam/navigation/ImuFactor.h>
#include <boost/smart_ptr/shared_ptr.hpp>
#include <boost/smart_ptr/make_shared.hpp>

// IMU sample time to live in seconds
#define IMU_TTL 1

// using PimUniquePtr = std::unique_ptr<gtsam::PreintegrationType>;
using PimUniquePtr = gtsam::PreintegrationType*;
using ImuBias = gtsam::imuBias::ConstantBias;
using namespace ILLIXR;

typedef struct {
	double timestamp;
	Eigen::Matrix<double, 3, 1> wm;
	Eigen::Matrix<double, 3, 1> am;
} imu_type;

class imu_integrator : public threadloop {
public:
	imu_integrator(std::string name_, phonebook* pb_)
		: threadloop{name_, pb_}
		, sb{pb->lookup_impl<switchboard>()}
		, _m_imu_cam{sb->subscribe_latest<imu_cam_type>("imu_cam")}
		, _m_in{sb->subscribe_latest<imu_integrator_seq>("imu_integrator_seq")}
		, _m_imu_integrator_input{sb->subscribe_latest<imu_integrator_input>("imu_integrator_input")}
        , _m_true_pose{sb->subscribe_latest<pose_type>("true_pose")}
		, _m_imu_raw{sb->publish<imu_raw_type>("imu_raw")}
        , _m_start_of_time{std::chrono::high_resolution_clock::now()}
		, first_true_pose_flag(false)
		, _seq_expect(1)
	{}

	virtual skip_option _p_should_skip() override {
		auto in = _m_in->get_latest_ro();
		if (!in || in->seq == _seq_expect-1) {
			// No new data, sleep to keep CPU utilization low
			std::this_thread::sleep_for(std::chrono::milliseconds{1});
			return skip_option::skip_and_yield;
		} else {
			if (in->seq != _seq_expect) {
				_stat_missed = in->seq - _seq_expect;
			} else {
				_stat_missed = 0;
			}
			_stat_processed++;
			_seq_expect = in->seq+1;
			return skip_option::run;
		}
	}

	void _p_one_iteration() override {
		const imu_cam_type *datum = _m_imu_cam->get_latest_ro();
		double timestamp_in_seconds = (double(datum->dataset_time) / NANO_SEC);

		imu_type data;
        data.timestamp = timestamp_in_seconds;
        data.wm = (datum->angular_v).cast<double>();
        data.am = (datum->linear_a).cast<double>();
		_imu_vec.emplace_back(data);

		clean_imu_vec(timestamp_in_seconds);
        propagate_imu_values(timestamp_in_seconds, datum->time);

        RAC_ERRNO_MSG("gtsam_integrator");
	}

private:
	const std::shared_ptr<switchboard> sb;

	// IMU Data, Sequence Flag, and State Vars Needed
	std::unique_ptr<reader_latest<imu_cam_type>> _m_imu_cam;
	std::unique_ptr<reader_latest<imu_integrator_seq>> _m_in;
	std::unique_ptr<reader_latest<imu_integrator_input>> _m_imu_integrator_input;
	
    std::unique_ptr<reader_latest<pose_type>> _m_true_pose;

	mutable Eigen::Quaternionf offset {Eigen::Quaternionf::Identity()};
	mutable std::shared_mutex offset_mutex;

	// Write IMU Biases for PP
	std::unique_ptr<writer<imu_raw_type>> _m_imu_raw;
	double first_true_pose[7];
    time_type _m_start_of_time;
	bool first_true_pose_flag;

	std::vector<imu_type> _imu_vec;
  	PimUniquePtr pim_ = NULL;

	[[maybe_unused]] double last_cam_time = 0;
	double last_imu_offset = 0;
	int64_t _seq_expect, _stat_processed, _stat_missed;

	// Remove IMU values older than 'IMU_TTL' from the imu buffer
	void clean_imu_vec(double timestamp) {
		auto imu_iterator = _imu_vec.begin();

		// Since the vector is ordered oldest to latest, keep deleting until you
		// hit a value less than 'IMU_TTL' seconds old
		while (imu_iterator != _imu_vec.end()) {
			if (timestamp-(*imu_iterator).timestamp < IMU_TTL) {
				break;
			}

			imu_iterator = _imu_vec.erase(imu_iterator);
		}
	}

	void _set_offset(const Eigen::Quaternionf& raw_o_times_offset){
        Eigen::Quaternionf raw_o = raw_o_times_offset * offset.inverse();
        //std::cout << "pose_prediction: set_offset" << std::endl;
        offset = raw_o.inverse();
    }

	Eigen::Quaternionf _apply_offset(const Eigen::Quaternionf& orientation) const {
        std::shared_lock lock {offset_mutex};
        return orientation * offset;
    }

	pose_type _correct_pose(const pose_type pose) const {
        pose_type swapped_pose;

        // Step 1: Compensate starting point to (0, 0, 0), pos only
        auto input_pose = pose_type{
            .sensor_time = pose.sensor_time,
            .position = Eigen::Vector3f{
                pose.position(0) - first_true_pose[0],
                pose.position(1) - first_true_pose[1],
                pose.position(2) - first_true_pose[2],
            },
            .orientation = pose.orientation,
        };

        // Step 3: Swap axis for both position and orientation
        // Step 3.1: Swap for position
        // This uses the OpenVINS standard output coordinate system.
        // This is a mapping between the OV coordinate system and the OpenGL system.
        swapped_pose.position.x() = -input_pose.position.x();
        swapped_pose.position.z() = input_pose.position.z();
        swapped_pose.position.y() = -input_pose.position.y();
		// swapped_pose.position.x() = input_pose.position.x();
        // swapped_pose.position.z() = input_pose.position.z();
        // swapped_pose.position.y() = input_pose.position.y();
		swapped_pose.orientation=input_pose.orientation;
        // Step 3.2: Swap for orientation
        // There is a slight issue with the orientations: basically,
        // the output orientation acts as though the "top of the head" is the
        // forward direction, and the "eye direction" is the up direction.
        Eigen::Quaternionf raw_o (input_pose.orientation.w(), -input_pose.orientation.y(), input_pose.orientation.z(), -input_pose.orientation.x());

        swapped_pose.orientation = _apply_offset(raw_o);

        return swapped_pose;
    }

	// Timestamp we are propagating the biases to (new IMU reading time)
	void propagate_imu_values(double timestamp, time_type real_time) {
		const imu_integrator_input *input_values = _m_imu_integrator_input->get_latest_ro();
		if (input_values == NULL) {
			return;
		}

		ImuBias imu_bias = ImuBias(input_values->biasAcc, input_values->biasGyro);
		if (pim_ == NULL) {
			boost::shared_ptr<gtsam::PreintegratedCombinedMeasurements::Params> params =
          			boost::make_shared<gtsam::PreintegratedCombinedMeasurements::Params>(input_values->params.n_gravity);

			params->setGyroscopeCovariance(std::pow(input_values->params.gyro_noise, 2.0) * Eigen::Matrix3d::Identity());
  			params->setAccelerometerCovariance(std::pow(input_values->params.acc_noise, 2.0) * Eigen::Matrix3d::Identity());
  			params->setIntegrationCovariance(std::pow(input_values->params.imu_integration_sigma, 2.0) * Eigen::Matrix3d::Identity());
			params->biasAccCovariance = std::pow(input_values->params.acc_walk, 2.0) * Eigen::Matrix3d::Identity();
			params->biasOmegaCovariance = std::pow(input_values->params.gyro_walk, 2.0) * Eigen::Matrix3d::Identity();

			// pim_ = std::make_unique<gtsam::PreintegratedCombinedMeasurements>(params, imu_bias);
			pim_ = new gtsam::PreintegratedCombinedMeasurements(params, imu_bias);
			last_imu_offset = input_values->t_offset;
		}

#ifndef NDEBUG
		if (input_values->last_cam_integration_time > last_cam_time) {
			std::cout << "New slow pose has arrived!\n";
			last_cam_time = input_values->last_cam_integration_time;
		}
#endif
		pim_->resetIntegrationAndSetBias(imu_bias);

		double time_begin = input_values->last_cam_integration_time + last_imu_offset;
		double time_end = timestamp + input_values->t_offset;

		std::vector<imu_type> prop_data = select_imu_readings(_imu_vec, time_begin, time_end);
		if (prop_data.size() < 2) {
			return;
		}

   		ImuBias prev_bias = pim_->biasHat();
		ImuBias bias = pim_->biasHat();

#ifndef NDEBUG
		std::cout << "Integrating over " << prop_data.size() << " IMU samples\n";
#endif

		for (unsigned i = 0; i < prop_data.size()-1; i++) {
			const gtsam::Vector3& measured_acc = prop_data.at(i).am;
			const gtsam::Vector3& measured_omega = prop_data.at(i).wm;

			// Delta T should be in seconds
			const double& delta_t = prop_data.at(i+1).timestamp - prop_data.at(i).timestamp;
			pim_->integrateMeasurement(measured_acc, measured_omega, delta_t);

			prev_bias = bias;
			bias = pim_->biasHat();
		}

		gtsam::NavState navstate_lkf(gtsam::Pose3(gtsam::Rot3(input_values->quat), input_values->position), input_values->velocity);
		gtsam::NavState navstate_k = pim_->predict(navstate_lkf, imu_bias);
		gtsam::Pose3 out_pose = navstate_k.pose();

#ifndef NDEBUG
		std::cout << "Base Position (x, y, z) = "
				<< input_values->position(0) << ", "
				<< input_values->position(1) << ", "
				<< input_values->position(2) << std::endl;

		std::cout << "New  Position (x, y, z) = "
				<< out_pose.x() << ", "
				<< out_pose.y() << ", "
				<< out_pose.z() << std::endl;
#endif
		std::cout<<"OUR SCHEME POSE!!!: "<<" "<</*real_time<<" "<<*/out_pose.translation().x()<<" "<<out_pose.translation().y()<<" "<<out_pose.translation().z()<<" "<<out_pose.rotation().toQuaternion().x()<<" "<<out_pose.rotation().toQuaternion().y()<<" "<<out_pose.rotation().toQuaternion().z()<<" "<<out_pose.rotation().toQuaternion().w()<<" "<<std::endl;
        const pose_type *true_pose = _m_true_pose->get_latest_ro();
		if (!first_true_pose_flag){
			first_true_pose[0]=true_pose->position[0];
			first_true_pose[1]=true_pose->position[1];
			first_true_pose[2]=true_pose->position[2];
			first_true_pose[3]=true_pose->orientation.x();
			first_true_pose[4]=true_pose->orientation.y();
			first_true_pose[5]=true_pose->orientation.z();
			first_true_pose[6]=true_pose->orientation.w();
		}

		pose_type corrected_true_pose=_correct_pose(*true_pose);
		if (!first_true_pose_flag){
			_set_offset(corrected_true_pose.orientation);
			first_true_pose_flag=true;
		}

        //std::cout<<"True POSE!!!: "<</*true_pose->sensor_time<<*/true_pose->position[0]-first_true_pose[0]<<true_pose->position[1]-first_true_pose[1]<<true_pose->position[2]-first_true_pose[2]<<true_pose->orientation.x()-first_true_pose[3]<<true_pose->orientation.y()-first_true_pose[4]<<true_pose->orientation.z()-first_true_pose[5]<<true_pose->orientation.w()-first_true_pose[6]<<std::endl;
        std::cout<<"True POSE!!!: "<<" "<</*true_pose->sensor_time<<" "<<*/corrected_true_pose.position[0]<<" "<<corrected_true_pose.position[1]<<" "<<corrected_true_pose.position[2]<<" "<<corrected_true_pose.orientation.x()<<" "<<corrected_true_pose.orientation.y()<<" "<<corrected_true_pose.orientation.z()<<" "<<corrected_true_pose.orientation.w()<<" "<<std::endl;
       
		_m_imu_raw->put(new imu_raw_type{
			prev_bias.gyroscope(),
			prev_bias.accelerometer(),
			bias.gyroscope(),
			bias.accelerometer(),
			out_pose.translation(), // Position
			navstate_k.velocity(), // Velocity
			out_pose.rotation().toQuaternion(), // Eigen Quat
			real_time
		});
	}

	// Select IMU readings based on timestamp similar to how OpenVINS selects IMU values to propagate
	std::vector<imu_type> select_imu_readings(const std::vector<imu_type>& imu_data, double time_begin, double time_end) {
		std::vector<imu_type> prop_data;
		if (imu_data.size() < 2) {
			return prop_data;
		}

		for (unsigned i = 0; i < imu_data.size()-1; i++) {

			// If time_begin comes inbetween two IMUs (A and B), interpolate A forward to time_begin
			if (imu_data.at(i+1).timestamp > time_begin && imu_data.at(i).timestamp < time_begin) {
				imu_type data = interpolate_imu(imu_data.at(i), imu_data.at(i+1), time_begin);
				prop_data.push_back(data);
				continue;
			}

			// IMU is within time_begin and time_end
			if (imu_data.at(i).timestamp >= time_begin && imu_data.at(i+1).timestamp <= time_end) {
				prop_data.push_back(imu_data.at(i));
				continue;
			}

			// IMU is past time_end
			if (imu_data.at(i+1).timestamp > time_end) {
				imu_type data = interpolate_imu(imu_data.at(i), imu_data.at(i+1), time_end);
				prop_data.push_back(data);
				break;
			}
		}

		// Loop through and ensure we do not have an zero dt values
		// This would cause the noise covariance to be Infinity
		for (size_t i = 0; i < prop_data.size()-1; i++) {
			if (std::abs(prop_data.at(i+1).timestamp-prop_data.at(i).timestamp) < 1e-12) {
				prop_data.erase(prop_data.begin()+i);
				i--;
			}
		}

		return prop_data;
	}

	// For when an integration time ever falls inbetween two imu measurements (modeled after OpenVINS)
	static imu_type interpolate_imu(const imu_type imu_1, imu_type imu_2, double timestamp) {
		imu_type data;
		data.timestamp = timestamp;

		double lambda = (timestamp - imu_1.timestamp) / (imu_2.timestamp - imu_1.timestamp);
		data.am = (1 - lambda) * imu_1.am + lambda * imu_2.am;
		data.wm = (1 - lambda) * imu_1.wm + lambda * imu_2.wm;

		return data;
	}
};

PLUGIN_MAIN(imu_integrator)
