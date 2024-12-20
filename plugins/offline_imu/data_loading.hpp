#pragma once
#include "illixr/csv_iterator.hpp"
#include "illixr/data_format/misc.hpp"
#include "illixr/switchboard.hpp"

#include <eigen3/Eigen/Dense>
#include <fstream>
#include <iostream>
#include <map>
#include <spdlog/spdlog.h>
#include <string>

typedef unsigned long long ullong;

typedef struct {
    Eigen::Vector3d angular_v;
    Eigen::Vector3d linear_a;
} raw_imu_type;

typedef struct {
    raw_imu_type imu0;
} sensor_types;

static std::map<ullong, sensor_types> load_data(const std::shared_ptr<ILLIXR::switchboard>& sb) {
    std::string illixr_data = sb->get_env("ILLIXR_DATA");
    if (illixr_data.empty()) {
        spdlog::get("illixr")->error("[offline_imu] Please define ILLIXR_DATA");
        ILLIXR::abort();
    }

    std::map<ullong, sensor_types> data;

    const std::string imu0_subpath = "/imu0/data.csv";
    std::ifstream     imu0_file{illixr_data + imu0_subpath};
    if (!imu0_file.good()) {
        spdlog::get("illixr")->error("[offline_imu] ${ILLIXR_DATA} {0} ({1}{0}) is not a good path", imu0_subpath, illixr_data);
        ILLIXR::abort();
    }
    for (CSVIterator row{imu0_file, 1}; row != CSVIterator{}; ++row) {
        ullong          t = std::stoull(row[0]);
        Eigen::Vector3d av{std::stod(row[1]), std::stod(row[2]), std::stod(row[3])};
        Eigen::Vector3d la{std::stod(row[4]), std::stod(row[5]), std::stod(row[6])};
        data[t].imu0 = {av, la};
    }

    return data;
}
