#include "common/error_util.hpp"
#include "csv_iterator.hpp"

#include <eigen3/Eigen/Dense>
#include <fstream>
#include <map>
#include <math.h>
#include <opencv2/core/mat.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <optional>
#include <string>

// timestamp
// p_RS_R_x [m], p_RS_R_y [m], p_RS_R_z [m]
// q_RS_w [], q_RS_x [], q_RS_y [], q_RS_z []
// v_RS_R_x [m s^-1], v_RS_R_y [m s^-1], v_RS_R_z [m s^-1]
// b_w_RS_S_x [rad s^-1], b_w_RS_S_y [rad s^-1], b_w_RS_S_z [rad s^-1]
// b_a_RS_S_x [m s^-2], b_a_RS_S_y [m s^-2], b_a_RS_S_z [m s^-2]

using namespace ILLIXR;

// typedef pose_type sensor_types;
typedef imu_integrator_input sensor_types;

static std::map<ullong, sensor_types> load_data() {
    const char* illixr_data_c_str = std::getenv("ILLIXR_DATA");
    if (!illixr_data_c_str) {
        ILLIXR::abort("Please define ILLIXR_DATA");
    }
    const std::string subpath     = "/state_groundtruth_estimate0/data.csv";
    std::string       illixr_data = std::string{illixr_data_c_str};

    std::map<ullong, sensor_types> data;

    std::ifstream gt_file{illixr_data + subpath};

    if (!gt_file.good()) {
        std::cerr << "${ILLIXR_DATA}" << subpath << " (" << illixr_data << subpath << ") is not a good path" << std::endl;
        ILLIXR::abort();
    }

	for(CSVIterator row{gt_file, 1}; row != CSVIterator{}; ++row) {
		ullong t = std::stoull(row[0]);
		Eigen::Vector3d ba {std::stod(row[14]), std::stod(row[15]), std::stod(row[16])};
		Eigen::Vector3d bg {std::stod(row[11]), std::stod(row[12]), std::stod(row[13])};
		Eigen::Vector3d pos {std::stod(row[1]), std::stod(row[2]), std::stod(row[3])};
		Eigen::Vector3d vel {std::stod(row[8]), std::stod(row[9]), std::stod(row[10])};
		Eigen::Quaterniond rot {std::stod(row[4]), std::stod(row[5]), std::stod(row[6]), std::stod(row[7])};
		data[t] = {
			time_point{}, 
			duration(std::chrono::milliseconds{-50}),
			imu_params{
				.gyro_noise = 0.00016968,
				.acc_noise = 0.002,
				.gyro_walk = 1.9393e-05,
				.acc_walk = 0.003,
				.n_gravity = Eigen::Matrix<double,3,1>(0.0, 0.0, -9.81),
				.imu_integration_sigma = 1.0,
				.nominal_rate = 200.0,
			},
			ba,
			bg,
			pos,
			vel,
			rot
		};
	}

    return data;
}
