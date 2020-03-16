#ifndef DATA_HH
#define DATA_HH

#include <iostream>
#include <chrono>
#include "GL/gl.h"
#include <GLFW/glfw3.h> // This is... ew. Look into making this more modular/generalized.

namespace ILLIXR {

	struct camera_frame {
		int pixel[1];
	};

	struct global_config {
		GLFWwindow* glfw_context;
	};

	// Designed to be a drop-in replacement for
	// the XrQuaternionf datatype. 
	struct quaternion_t {
		float x,y,z,w;
	};

	// Designed to be a drop-in replacement for
	// the XrVector3f datatype. 
	struct vector3_t {
		float x,y,z;
	};

	// Designed to be a drop-in replacement for
	// the XrPosef datatype. 
	struct pose_t {
		quaternion_t orientation;
		vector3_t position;
	};

	// A particular pose, sampled at a particular point in time.
	struct pose_sample {
		pose_t pose;
		std::chrono::time_point<std::chrono::system_clock> sample_time; 
	};

	// Struct that will hold the linear accel and gyro readings from an IMU
	struct imu_measurement_t {
		float ax, ay, az;
		float gx, gy, gz;
	};

	// An IMU reading sampled at a particular point in time
	struct imu_sample {
		imu_measurement_t measurement;
		std::chrono::time_point<std::chrono::system_clock> sample_time;
	};

	struct rendered_frame {
		GLuint texture_handle;
		pose_sample render_pose; // The pose used when rendering this frame.
		std::chrono::time_point<std::chrono::system_clock> sample_time; 
	};

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
