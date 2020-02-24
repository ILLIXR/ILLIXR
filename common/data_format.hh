#ifndef DATA_HH
#define DATA_HH

#include <iostream>

namespace ILLIXR {

struct camera_frame {
	int pixel[1];
};

/* I use "accel" instead of "3-vector" as a datatype, because
   this checks that you meant to use an acceleration in a certain
   place. */
struct accel { };

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

struct rendered_frame { };

}

#endif
