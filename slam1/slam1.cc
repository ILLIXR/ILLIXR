#include <functional>

#include "common/component.hh"
#include "common/switchboard.hh"
#include "common/data_format.hh"

#include <okvis/VioParametersReader.hpp>
#include <okvis/ThreadedKFVio.hpp>
#include <opencv2/core/mat.hpp>
#include <eigen3/Eigen/Dense>

using namespace ILLIXR;

class slam1 : public component {
public:
	/* Provide handles to slam1 */
	slam1(std::unique_ptr<writer<pose_type>>&& pose, okvis::VioParameters parameters)
		: _m_pose{std::move(pose)}
		, _m_begin{std::chrono::system_clock::now()}
		, okvis_estimator{parameters}
	{
		okvis_estimator.setFullStateCallback(
			std::bind(
					  &slam1::publish_results,
					  this,
					  std::placeholders::_1,
					  std::placeholders::_2,
					  std::placeholders::_3,
					  std::placeholders::_4));

		okvis_estimator.setBlocking(true);

	}

	/* In the future, could add more based off of what is in the
	   okvis::kinematics::Transformation type */
	void publish_results(
						 const okvis::Time & t,
						 const okvis::kinematics::Transformation & T_WS,
						 const Eigen::Matrix<double, 9, 1> & /*speedAndBiases*/,
						 const Eigen::Matrix<double, 3, 1> & /*omega_S*/) {
		_m_pose->put(new pose_type{
			T_WS.r(),
			T_WS.q(),
			T_WS.C(),
		});
		/* Publish this to a topic */
	}

	void feed_cam(const cam_type* cam_frame) {
		okvis_estimator.addImage(cvtTime(cam_frame->time), cam_frame->id, *cam_frame->img);
	}

	void feed_imu(const imu_type* imu_reading) {
		okvis_estimator.addImuMeasurement(cvtTime(imu_reading->time), imu_reading->linear_a, imu_reading->angular_v);
	}

	virtual void _p_start() override {
		/* All of my work is already scheduled synchronously. Nohting to do here. */
	}

	virtual void _p_stop() override { }

	virtual ~slam1() override { }

private:
	std::unique_ptr<writer<pose_type>> _m_pose;
	time_type _m_begin;

	// break naming convention so that I can copy/paste code from
	// okvis/okvis_apps/src/okvis_app_synchronous.cpp
	okvis::ThreadedKFVio okvis_estimator;

	okvis::Time cvtTime(time_type t) {
		auto diff = t - _m_begin;
		auto nanosecs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(diff).count());
		auto secs = static_cast<uint32_t>(nanosecs / 1000000);
		auto nanosecs_rem = static_cast<uint32_t>(nanosecs - secs * 1000000);
		return {secs, nanosecs_rem};
	}

};

extern "C" component* create_component(switchboard* sb) {
	/* First, we declare intent to read/write topics. Switchboard
	   returns handles to those topics. */
	auto pose_ev = sb->publish<pose_type>("pose");

	std::string configFilename {"./slam1/okvis/config/config_fpga_p2_euroc.yaml"};
	okvis::VioParametersReader vio_parameters_reader {configFilename};
	okvis::VioParameters parameters;
	vio_parameters_reader.getParameters(parameters);

	auto this_slam1 = new slam1{std::move(pose_ev), parameters};

	sb->schedule<cam_type>("cams", std::bind(&slam1::feed_cam, this_slam1, std::placeholders::_1));
	sb->schedule<imu_type>("imu0", std::bind(&slam1::feed_imu, this_slam1, std::placeholders::_1));

	return this_slam1;
}
