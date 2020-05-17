#include "phonebook.hpp"
#include "data_format.hpp"

using namespace ILLIXR;

class pose_correction : public service {
public:

    // In the future this service will be pose predict which will predict a pose some t in the future
    pose_type* correct_pose(pose_type* pose) {
        pose_type* swapped_pose = new pose_type(*pose);

        // This uses the OpenVINS standard output coordinate system.
        // This is a mapping between the OV coordinate system and the OpenGL system.
        swapped_pose->position.x() = -pose->position.y();
        swapped_pose->position.y() = pose->position.z();
        swapped_pose->position.z() = -pose->position.x();

        // There is a slight issue with the orientations: basically,
        // the output orientation acts as though the "top of the head" is the
        // forward direction, and the "eye direction" is the up direction.
        // Can be offset with an initial "calibration quaternion."
        swapped_pose->orientation.w() = pose->orientation.w();
        swapped_pose->orientation.x() = -pose->orientation.y();
        swapped_pose->orientation.y() = pose->orientation.z();
        swapped_pose->orientation.z() = -pose->orientation.x();

        return swapped_pose;
    }
};