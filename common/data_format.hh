#ifndef DATA_HH
#define DATA_HH

#include <iostream>

#include <opencv2/core/mat.hpp>
#include <eigen3/Eigen/Dense>

namespace ILLIXR {

typedef std::chrono::time_point<std::chrono::system_clock> time_type;

typedef struct {
	time_type time;
	Eigen::Vector3d angular_v;
	Eigen::Vector3d linear_a;
} imu_type;

typedef struct {
	time_type time;
	std::unique_ptr<cv::Mat> img;
	unsigned char id;
} cam_type;

typedef struct {
	Eigen::Vector3d position;
	Eigen::Quaterniond orientation;
	Eigen::Matrix3d rot_mat;
} pose_type;

struct rendered_frame { };

}

#endif
