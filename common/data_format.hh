#ifndef DATA_HH
#define DATA_HH

#include <iostream>
#include <chrono>
#include <memory>

#include <opencv2/core/mat.hpp>
#include <eigen3/Eigen/Dense>

namespace ILLIXR {
	typedef std::chrono::time_point<std::chrono::system_clock> time_type;

	typedef struct {
		time_type time;
		Eigen::Vector3f angular_v;
		Eigen::Vector3f linear_a;
	} imu_type;

	typedef struct {
		time_type time;
		std::unique_ptr<cv::Mat> img0;
		std::unique_ptr<cv::Mat> img1;
	} cam_type;

	typedef struct {
		time_type time; 
		Eigen::Vector3f position;
		Eigen::Quaternionf orientation;
	} pose_type;

	typedef struct {
		int pixel[1];
	} camera_frame;

	typedef struct {
		GLFWwindow* glfw_context;
	} global_config;

	typedef struct {
		GLuint texture_handle;
		pose_type render_pose; // The pose used when rendering this frame.
		std::chrono::time_point<std::chrono::system_clock> sample_time; 
	} rendered_frame;
}

#endif
