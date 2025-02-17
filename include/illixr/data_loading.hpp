#pragma once

#include "illixr/csv_iterator.hpp"
#include "illixr/data_format.hpp"
#include "illixr/error_util.hpp"

#include <eigen3/Eigen/Dense>
#include <fstream>
#include <iostream>
#include <map>
#include <spdlog/spdlog.h>
#include <string>

using namespace ILLIXR;

template<typename T>
static std::map<ullong, T> load_data(const std::string& spath, const std::string& plugin_name,
                                     std::map<ullong, T> (*func)(std::ifstream&, const std::string&)) {
    const char* illixr_data_c_str = std::getenv("ILLIXR_DATA");
    if (!illixr_data_c_str) {
        throw std::runtime_error("Please define ILLIXR_DATA");
    }
    const std::string subpath     = "/" + spath + "/data.csv";
    std::string       illixr_data = std::string{illixr_data_c_str};

    std::ifstream gt_file{illixr_data + subpath};

    if (!gt_file.good()) {
        spdlog::get("illixr")->error("[{0}] ${ILLIXR_DATA}{1} ({2}{1}) is not a good path", plugin_name, subpath, illixr_data);
        throw std::runtime_error("[" + plugin_name + "] ${ILLIXR_DATA}" + illixr_data + " (" + subpath + illixr_data +") is not a good path");
    }

    return func(gt_file, illixr_data + subpath);
}
