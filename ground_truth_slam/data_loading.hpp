#include <map>
#include <fstream>
#include <string>
#include <optional>
#include <math.h>

#include <opencv2/core/mat.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <eigen3/Eigen/Dense>

#include "csv_iterator.hpp"
#include "common/global_module_defs.hpp"

#include "common/error_util.hpp"

// timestamp
// p_RS_R_x [m], p_RS_R_y [m], p_RS_R_z [m]
// q_RS_w [], q_RS_x [], q_RS_y [], q_RS_z []
// v_RS_R_x [m s^-1], v_RS_R_y [m s^-1], v_RS_R_z [m s^-1]
// b_w_RS_S_x [rad s^-1], b_w_RS_S_y [rad s^-1], b_w_RS_S_z [rad s^-1]
// b_a_RS_S_x [m s^-2], b_a_RS_S_y [m s^-2], b_a_RS_S_z [m s^-2]

using namespace ILLIXR;

typedef pose_type sensor_types;

static
std::map<ullong, sensor_types>
load_data() {
	constexpr std::string subpath {"/state_groundtruth_estimate0/data.csv"};
	std::string illixr_data {ILLIXR::DATA_PATH};

	std::map<ullong, sensor_types> data;

	std::ifstream gt_file {illixr_data + subpath};

	if (!gt_file.good()) {
		std::cerr << "'" << illixr_data << subpath << "' is not a good path" << std::endl;
        ILLIXR::abort();
	}

	for(CSVIterator row{gt_file, 1}; row != CSVIterator{}; ++row) {
		ullong t = std::stoull(row[0]);
		Eigen::Vector3f av {std::stof(row[1]), std::stof(row[2]), std::stof(row[3])};
		Eigen::Quaternionf la {std::stof(row[4]), std::stof(row[5]), std::stof(row[6]), std::stof(row[7])};
		data[t] = {{}, av, la};
	}

	return data;
}
