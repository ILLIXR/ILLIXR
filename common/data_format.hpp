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
	typedef struct {
		time_type time;
		Eigen::Vector3f angular_v;
		Eigen::Vector3f linear_a;
		std::optional<cv::Mat*> img0;
		std::optional<cv::Mat*> img1;
		ullong dataset_time;
	} imu_cam_type;

	typedef struct {
		Eigen::Matrix<double,3,1> w_hat;
		Eigen::Matrix<double,3,1> a_hat;
		Eigen::Matrix<double,3,1> w_hat2;
		Eigen::Matrix<double,3,1> a_hat2;
		Eigen::Matrix<double,13,1> state_plus;
		time_type imu_time;
	} imu_raw_type;

	typedef struct {
	  int64_t time;
	  const unsigned char* rgb;
	  const unsigned short* depth;
	} rgb_depth_type;

	typedef struct {
		time_type sensor_time; // Recorded time of sensor data ingestion
		Eigen::Vector3f position;
		Eigen::Quaternionf orientation;
	} pose_type;

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
	struct rendered_frame {
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

	typedef struct {
		int seq;		
	} imu_integrator_seq;

	typedef struct {
		bool slam_ready;
		double t_offset;
		double last_cam_integration_time;
		Eigen::VectorXd imu_value;
		Eigen::VectorXd imu_fej;
		Eigen::Matrix<double,3,1> gravity;
	} imu_integrator_input;

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
