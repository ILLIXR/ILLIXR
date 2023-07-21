#include "common/plugin.hpp"

#include "ground_truth_publisher.hpp"
#include "image_publisher.hpp"
#include "imu_publisher.hpp"
#include "pose_publisher.hpp"

PLUGIN_MAIN(IMUPublisher);
PLUGIN_MAIN(ImagePublisher);
PLUGIN_MAIN(PosePublisher);
PLUGIN_MAIN(GroundTruthPublisher);
