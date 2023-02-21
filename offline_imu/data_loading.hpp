#include "common/csv_iterator.hpp"

#include <eigen3/Eigen/Dense>
#include <fstream>
#include <map>
#include <optional>
#include <string>

typedef unsigned long long ullong;

typedef struct {
    Eigen::Vector3d angular_v;
    Eigen::Vector3d linear_a;
} raw_imu_type;

typedef struct {
    raw_imu_type imu0;
} sensor_types;

static std::map<ullong, sensor_types> load_data() {
    const char* illixr_data_c_str = std::getenv("ILLIXR_DATA");
    if (!illixr_data_c_str) {
        std::cerr << "Please define ILLIXR_DATA" << std::endl;
        ILLIXR::abort();
    }
    std::string illixr_data = std::string{illixr_data_c_str};

    std::map<ullong, sensor_types> data;

    const std::string imu0_subpath_asl    = "/imu0/data.csv";
    const std::string imu0_subpath_kalibr = "/imu0.csv";
    std::ifstream     imu0_file_asl{illixr_data + imu0_subpath_asl};
    std::ifstream     imu0_file_kalibr{illixr_data + imu0_subpath_kalibr};
    if (!imu0_file_asl.good() && !imu0_file_kalibr.good()) {
        std::cerr << "${ILLIXR_DATA}" << imu0_subpath_asl << " (" << illixr_data << imu0_subpath_asl << ") is not a good path"
                  << std::endl;
        std::cerr << "${ILLIXR_DATA}" << imu0_subpath_kalibr << " (" << illixr_data << imu0_subpath_kalibr
                  << ") is not a good path" << std::endl;
        ILLIXR::abort();
    }

    if (imu0_file_asl.good()) {
        for (CSVIterator row{imu0_file_asl, 1}; row != CSVIterator{}; ++row) {
            ullong          t = std::stoull(row[0]);
            Eigen::Vector3d av{std::stod(row[1]), std::stod(row[2]), std::stod(row[3])};
            Eigen::Vector3d la{std::stod(row[4]), std::stod(row[5]), std::stod(row[6])};
            data[t].imu0 = {av, la};
        }
    }

    if (imu0_file_kalibr.good()) {
        for (CSVIterator row{imu0_file_kalibr, 1}; row != CSVIterator{}; ++row) {
            ullong          t = std::stoull(row[0]);
            Eigen::Vector3d av{std::stod(row[1]), std::stod(row[2]), std::stod(row[3])};
            Eigen::Vector3d la{std::stod(row[4]), std::stod(row[5]), std::stod(row[6])};
            data[t].imu0 = {av, la};
        }
    }

    return data;
}
