namespace ILLIXR {

struct cam_frame { };

/* I use "accel" instead of "3-vector" as a datatype, because
   this checks that you meant to use an acceleration in a certain
   place. */
struct accel { };

struct pose {
	int data[6];
};

struct rendered_frame { };

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

class abstract_slam {
public:
	virtual void feed_cam_frame_nonbl(cam_frame&) = 0;
	virtual void feed_accel_nonbl(accel&) = 0;
	virtual pose& produce_nonbl() = 0;
	virtual ~abstract_slam() { };
};

}

#define ILLIXR_make_dynamic_factory(abstract_type, implementation) \
	extern "C" abstract_type* make_##abstract_type() { return new implementation; }
