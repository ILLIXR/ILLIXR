#ifndef DATA_HH
#define DATA_HH

#include <iostream>
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

	struct rendered_frame {
		GLuint texture_handle;
		pose_t render_pose; // The pose used when rendering this frame.
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

	class pose {
	public:
		int data[3];
	};

	std::ostream& operator<<(std::ostream& out, const pose& pose) {
		return out << "pose{"
				<< pose.data[0] << ", "
				<< pose.data[1] << ", "
				<< pose.data[2] << "}";
	}

}

#endif
