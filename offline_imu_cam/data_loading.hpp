#include <map>
#include <fstream>
#include <string>
#include <optional>
#include <cassert>

#include <opencv2/core/mat.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <eigen3/Eigen/Dense>

#include "csv_iterator.hpp"
#include "common/error_util.hpp"
#include "common/global_module_defs.hpp"


typedef unsigned long long ullong;

typedef struct {
	Eigen::Vector3d angular_v;
	Eigen::Vector3d linear_a;
} raw_imu_type;

class lazy_load_image {
public:
	lazy_load_image(const std::string& path)
		: _m_path(path)
	{ }
	std::unique_ptr<cv::Mat> load() const {
		auto img = std::unique_ptr<cv::Mat>{new cv::Mat{cv::imread(_m_path, cv::IMREAD_GRAYSCALE)}};
		assert(!img->empty());
		return img;
	}

private:
	std::string _m_path;
};

typedef struct {
	std::optional<raw_imu_type> imu0;
	std::optional<lazy_load_image> cam0;
	std::optional<lazy_load_image> cam1;
} sensor_types;

static
std::map<ullong, sensor_types>
load_data(const std::string& data_path) {
	std::map<ullong, sensor_types> data;

	const std::string imu0_subpath {"/imu0/data.csv"};
	std::ifstream imu0_file {data_path + imu0_subpath};
	if (!imu0_file.good()) {
		std::cerr << "'" << data_path << imu0_subpath << "' is not a good path" << std::endl;
        ILLIXR::abort();
	}
	for (CSVIterator row{imu0_file, 1}; row != CSVIterator{}; ++row) {
		ullong t = std::stoull(row[0]);
		Eigen::Vector3d av {std::stod(row[1]), std::stod(row[2]), std::stod(row[3])};
		Eigen::Vector3d la {std::stod(row[4]), std::stod(row[5]), std::stod(row[6])};
		data[t].imu0 = {av, la};
	}

	const std::string cam0_subpath {"/cam0/data.csv"};
	std::ifstream cam0_file {data_path + cam0_subpath};
	if (!cam0_file.good()) {
		std::cerr << "'" << data_path << cam0_subpath << "' is not a good path" << std::endl;
        ILLIXR::abort();
	}
	for (CSVIterator row{cam0_file, 1}; row != CSVIterator{}; ++row) {
		ullong t = std::stoull(row[0]);
		data[t].cam0 = {data_path + "/cam0/data/" + row[1]};
	}

	const std::string cam1_subpath {"/cam1/data.csv"};
	std::ifstream cam1_file {data_path + cam1_subpath};
	if (!cam1_file.good()) {
		std::cerr << "'" << data_path << cam1_subpath << "' is not a good path" << std::endl;
        ILLIXR::abort();
	}
	for (CSVIterator row{cam1_file, 1}; row != CSVIterator{}; ++row) {
		ullong t = std::stoull(row[0]);
		std::string fname = row[1];
		data[t].cam1 = {data_path + "/cam1/data/" + row[1]};
	}

	return data;
}
