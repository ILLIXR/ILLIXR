#include <map>
#include <fstream>
#include <string>
#include <optional>

#include <opencv/cv.hpp>
#include <eigen3/Eigen/Dense>

#include "csv_iterator.hh"

class lazy_load_image {
public:
	lazy_load_image(const std::string& path)
		: _m_path(path)
	{ }
	cv::Mat* load() const {
		cv::Mat* img = new cv::Mat{cv::imread(_m_path, cv::IMREAD_COLOR)};
		cv::cvtColor(*img, *img, cv::COLOR_BGR2GRAY);
		assert(!img->empty());
		return img;
	}

private:
	std::string _m_path;
};

typedef Eigen::Matrix<double, 3, 1> vec3;
typedef struct {
	std::optional<std::pair<vec3, vec3>> imu0;
	std::optional<lazy_load_image> cam0;
	std::optional<lazy_load_image> cam1;
} sensor_types;

static
std::map<double, sensor_types>&&
load_data(const std::string& data_path) {
	std::map<double, sensor_types> data;

	std::ifstream imu0_file {data_path + "imu0/data.csv"};
	for(CSVIterator row{imu0_file, 1}; row != CSVIterator{}; ++row) {
		double t = std::stod(row[0]);
		vec3 av {std::stod(row[1]), std::stod(row[2]), std::stod(row[3])};
		vec3 la {std::stod(row[4]), std::stod(row[5]), std::stod(row[6])};
		data[t].imu0 = std::make_pair(av, la);
	}

	std::ifstream cam0_file {data_path + "cam0/data.csv"};
	for(CSVIterator row{cam0_file, 1}; row != CSVIterator{}; ++row) {
		double t = std::stod(row[0]);
		data[t].cam0 = lazy_load_image{data_path + "cam0/" + row[1]};
	}

	std::ifstream cam1_file{data_path + "cam1/data.csv"};
	for(CSVIterator row{cam1_file, 1}; row != CSVIterator{}; ++row) {
		double t = std::stod(row[0]);
		std::string fname = row[1];
		data[t].cam1 = lazy_load_image{data_path + "cam1/" + row[1]};
	}

	return std::move(data);
}
