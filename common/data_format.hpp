#ifndef DATA_HH
#define DATA_HH

#include <iostream>
#include <chrono>
#include <memory>
#include <boost/optional.hpp>

#include <opencv2/core/mat.hpp>
#undef Success // For 'Success' conflict
#include <eigen3/Eigen/Dense>
#include <GL/gl.h>
#include <GL/glx.h>
#undef Complex // For 'Complex' conflict
#include "phonebook.hpp"
#include "switchboard.hpp"

// Tell gldemo and timewarp_gl to use two texture handle for left and right eye
#define USE_ALT_EYE_FORMAT

namespace ILLIXR {

	typedef std::chrono::time_point<std::chrono::system_clock> time_type;
	typedef unsigned long long ullong;

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
		Eigen::Vector3f angular_v;
		Eigen::Vector3f linear_a;
		std::optional<std::unique_ptr<cv::Mat>> img0;
		std::optional<std::unique_ptr<cv::Mat>> img1;
		ullong temp_time;
	} imu_cam_type;

	typedef struct {
		time_type time; 
		Eigen::Vector3f position;
		Eigen::Quaternionf orientation;
	} pose_type;

	typedef struct {
		int pixel[1];
	} camera_frame;

/*	class global_config : public service {
	public:
		global_config(GLFWwindow* _glfw_context) : glfw_context(_glfw_context) { }
		GLFWwindow* glfw_context;
	};*/

	class xlib_gl_extended_window : public service {
	public:
		xlib_gl_extended_window(int _width, int _height, GLXContext _shared_gl_context);
		int 					width;
		int 					height;
		Display                 *dpy;
		Window                  win;
		GLXContext              glc;
	};

	// A particular pose, sampled at a particular point in time.
	// struct pose_sample {
	// 	pose_type pose;
	// 	std::chrono::time_point<std::chrono::system_clock> sample_time; 
	// };

	// Single-texture format; arrayed by left/right eye
	// Single-texture format; arrayed by left/right eye
	struct rendered_frame {
		GLuint texture_handle;
		pose_type render_pose; // The pose used when rendering this frame.
		std::chrono::time_point<std::chrono::system_clock> sample_time; 
	};

	// Using arrays as a swapchain
	// Array of left eyes, array of right eyes
	// This more closely matches the format used by Monado
	struct rendered_frame_alt {
		GLuint texture_handles[2]; // Does not change between swaps in swapchain
		GLuint swap_indices[2]; // Which element of the swapchain
		pose_type render_pose; // The pose used when rendering this frame.
		std::chrono::time_point<std::chrono::system_clock> sample_time; 
	};

	typedef struct {
		int seq;
	} hologram_input;

	typedef struct {
		int dummy;
	} hologram_output;

	/* I use "accel" instead of "3-vector" as a datatype, because
	this checks that you meant to use an acceleration in a certain
	place. */
	struct accel { };

	// High-level HMD specification, timewarp component
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

#endif
