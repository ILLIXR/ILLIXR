#ifndef DATA_HH
#define DATA_HH

#include <iostream>
#include <chrono>

#include <opencv2/core/mat.hpp>
#include <eigen3/Eigen/Dense>

#include "GL/gl.h"
#include <GLFW/glfw3.h> // This is... ew. Look into making this more modular/generalized.

namespace ILLIXR {
	typedef std::chrono::time_point<std::chrono::system_clock> time_type;

	typedef struct {
		time_type time;
		Eigen::Vector3d angular_v;
		Eigen::Vector3d linear_a;
	} imu_type;

	typedef struct {
		time_type time;
		std::unique_ptr<cv::Mat> img0;
		std::unique_ptr<cv::Mat> img1;
	} cam_type;

	typedef struct {
		Eigen::Vector3d position;
		Eigen::Quaterniond orientation;
		time_type time; 
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
