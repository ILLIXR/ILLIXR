#include "GL/gl.h"
#include <GLFW/glfw3.h>

namespace ILLIXR {

	struct cam_frame { };

	/* I use "accel" instead of "3-vector" as a datatype, because
	this checks that you meant to use an acceleration in a certain
	place. */
	struct accel { };

	struct pose {
		int data[6];
	};

	struct rendered_frame {
		GLuint texture_handle;
	};

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

	/* All methods compiled-in separately have to be virtual. */

	class abstract_cam {
	public:
		virtual cam_frame& produce_blocking() = 0;
		virtual ~abstract_cam() { }
	};

	class abstract_imu {
	public:
		virtual accel& produce_nonbl() = 0;
		virtual ~abstract_imu() { }
	};

	class abstract_timewarp {
	public:
		virtual void init(rendered_frame frame_handle, GLFWwindow* shared_context, hmd_physical_info* hmd) = 0;
		virtual void warp(float time) = 0;
		virtual ~abstract_timewarp() { }
	};

		/* In this implementation, all of the asynchrony happens inside
		the components. feed_cam_frame_nobl could add a camera frame to
		a queue. produce_nobl can be read from a double buffer. */
	class abstract_slam {
	public:
		virtual void feed_cam_frame_nonbl(cam_frame&) = 0;
		virtual void feed_accel_nonbl(accel&) = 0;
		virtual pose& produce_nonbl() = 0;
		virtual ~abstract_slam() { }
	};

}

#define ILLIXR_make_dynamic_factory(abstract_type, implementation) \
	extern "C" abstract_type* make_##abstract_type() { return new implementation; }
