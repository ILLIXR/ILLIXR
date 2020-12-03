#pragma once

#include <iostream>
#include <chrono>
#include <memory>
#include <boost/optional.hpp>

#include <opencv2/core/mat.hpp>
#undef Success // For 'Success' conflict
#include <eigen3/Eigen/Dense>
#include <GL/gl.h>
#include <GLFW/glfw3.h>
//#undef Complex // For 'Complex' conflict
#include "phonebook.hpp" 
#include "switchboard.hpp"

// Tell gldemo and timewarp_gl to use two texture handle for left and right eye
#define USE_ALT_EYE_FORMAT
#define NANO_SEC 1000000000.0

namespace ILLIXR {

	typedef std::chrono::time_point<std::chrono::system_clock> time_type;
	typedef unsigned long long ullong;

	// Data type that combines the IMU and camera data at a certain timestamp.
	// If there is only IMU data for a certain timestamp, img0 and img1 will be null
	// time is the current UNIX time where dataset_time is the time read from the csv
	struct imu_cam_type : switchboard::event {
		time_type time;
		Eigen::Vector3f angular_v;
		Eigen::Vector3f linear_a;
		std::optional<cv::Mat> img0;
		std::optional<cv::Mat> img1;
		ullong dataset_time;
		imu_cam_type(time_type time_,
					 Eigen::Vector3f angular_v_,
					 Eigen::Vector3f linear_a_,
					 std::optional<cv::Mat> img0_,
					 std::optional<cv::Mat> img1_,
					 ullong dataset_time_)
			: time{time_}
			, angular_v{angular_v_}
			, linear_a{linear_a_}
			, img0{img0_}
			, img1{img1_}
			, dataset_time{dataset_time_}
		{ }
	};

    typedef struct {
        std::optional<cv::Mat*> rgb;
        std::optional<cv::Mat*> depth;
        ullong timestamp;
    } rgb_depth_type3;

	// Values needed to initialize the IMU integrator
	typedef struct {
		double gyro_noise;
		double acc_noise;
		double gyro_walk;
		double acc_walk;
		Eigen::Matrix<double,3,1> n_gravity;
		double imu_integration_sigma;
		double nominal_rate;
	} imu_params;

	// IMU biases, initialization params, and slow pose needed by the IMU integrator
	struct imu_integrator_input : public switchboard::event {
		double last_cam_integration_time;
		double t_offset;
		imu_params params;
		
		Eigen::Vector3d biasAcc;
		Eigen::Vector3d biasGyro;
		Eigen::Matrix<double,3,1> position;
		Eigen::Matrix<double,3,1> velocity;
		Eigen::Quaterniond quat;
		imu_integrator_input(
							 double last_cam_integration_time_,
							 double t_offset_,
							 imu_params params_,
							 Eigen::Vector3d biasAcc_,
							 Eigen::Vector3d biasGyro_,
							 Eigen::Matrix<double,3,1> position_,
							 Eigen::Matrix<double,3,1> velocity_,
							 Eigen::Quaterniond quat_
							 )
			: last_cam_integration_time{last_cam_integration_time_}
			, t_offset{t_offset_}
			, params{params_}
			, biasAcc{biasAcc_}
			, biasGyro{biasGyro_}
			, position{position_}
			, velocity{velocity_}
			, quat{quat_}
		{ }
	};

	// Output of the IMU integrator to be used by pose prediction
	struct imu_raw_type : switchboard::event{
		// Biases from the last two IMU integration iterations used by RK4 for pose predict
		Eigen::Matrix<double,3,1> w_hat;
		Eigen::Matrix<double,3,1> a_hat;
		Eigen::Matrix<double,3,1> w_hat2;
		Eigen::Matrix<double,3,1> a_hat2;

		// Faster pose propagated forwards by the IMU integrator
		Eigen::Matrix<double,3,1> pos;
		Eigen::Matrix<double,3,1> vel;
		Eigen::Quaterniond quat;
		time_type imu_time;
		imu_raw_type(Eigen::Matrix<double,3,1> w_hat_,
					 Eigen::Matrix<double,3,1> a_hat_,
					 Eigen::Matrix<double,3,1> w_hat2_,
					 Eigen::Matrix<double,3,1> a_hat2_,
					 Eigen::Matrix<double,3,1> pos_,
					 Eigen::Matrix<double,3,1> vel_,
					 Eigen::Quaterniond quat_,
					 time_type imu_time_)
			: w_hat{w_hat_}
			, a_hat{a_hat_}
			, w_hat2{w_hat2_}
			, a_hat2{a_hat2_}
			, pos{pos_}
			, vel{vel_}
			, quat{quat_}
			, imu_time{imu_time_}
		{ }
	};

	typedef struct {
	  int64_t time;
	  const unsigned char* rgb;
	  const unsigned short* depth;
	} rgb_depth_type2;

	struct pose_type : switchboard::event {
		time_type sensor_time; // Recorded time of sensor data ingestion
		Eigen::Vector3f position;
		Eigen::Quaternionf orientation;
		pose_type() { }
		pose_type(time_type sensor_time_,
				  Eigen::Vector3f position_,
				  Eigen::Quaternionf orientation_)
			: sensor_time{sensor_time_}
			, position{position_}
			, orientation{orientation_}
		{ }
	};

	typedef struct {
		pose_type pose;
		time_type predict_computed_time; // Time at which the prediction was computed
		time_type predict_target_time; // Time that prediction targeted.
	} fast_pose_type;

	typedef struct {
		int pixel[1];
	} camera_frame;

	// Using arrays as a swapchain
	// Array of left eyes, array of right eyes
	// This more closely matches the format used by Monado
	struct rendered_frame : switchboard::event {
		GLuint texture_handles[2]; // Does not change between swaps in swapchain
		GLuint swap_indices[2]; // Which element of the swapchain
		fast_pose_type render_pose; // The pose used when rendering this frame.
		std::chrono::time_point<std::chrono::system_clock> sample_time;
		std::chrono::time_point<std::chrono::system_clock> render_time;
	};

	typedef struct {
		int seq;
	} hologram_input;

	typedef struct {
		int dummy;
	} hologram_output;

	/*
	class sequence : public switchboard::event {
		bool operator==(const sequence& other) { return i == other.i; }
		bool operator!=(const sequence& other) { return i != other.i; }
		bool operator<=(const sequence& other) { return i <= other.i; }
		bool operator< (const sequence& other) { return i <  other.i; }
		bool operator>=(const sequence& other) { return i >= other.i; }
		bool operator> (const sequence& other) { return i >  other.i; }
		int64_t operator-(const sequence& other) { return i - other.i; }
		sequence operator+(int64_t other) { return sequence{i + other}; }
		void increment() {
			++i;
		}
	private:
		sequence(int64_t _i) : i{_i} { }
		int64_t i = 0;
	};
*/

	/* I use "accel" instead of "3-vector" as a datatype, because
	this checks that you meant to use an acceleration in a certain
	place. */
	struct accel { };

	// High-level HMD specification, timewarp plugin
	// may/will calculate additional HMD info based on these specifications
	struct hmd_physical_info {
		float   ipd;
		int		displayPixelsWide;
		int		displayPixelsHigh;
		float	chromaticAberration[4];
		float	K[11];
		int		visiblePixelsWide;
		int		visiblePixelsHigh;
		float	visibleMetersWide;
		float	visibleMetersHigh;
		float	lensSeparationInMeters;
		float	metersPerTanAngleAtCenter;
	};
}
