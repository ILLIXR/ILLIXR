#pragma once

#include "illixr/data_format/misc.hpp"
#include "illixr/error_util.hpp"
#include "illixr/switchboard.hpp"

#include <eigen3/Eigen/Dense>
#include <fstream>
#include <iostream>
#include <map>
#include <spdlog/spdlog.h>
#include <string>

using namespace ILLIXR;

template<typename T>
static std::map<ullong, T> load_data(const std::string& spath, const std::string& plugin_name,
                                     std::map<ullong, T> (*func)(std::ifstream&, const std::string&),
                                     const std::shared_ptr<switchboard>& sb,
                                     const std::string& file_name = "data.csv") {
    const char* illixr_data_c_str = sb->get_env_char("ILLIXR_DATA");
    if (!illixr_data_c_str) {
        ILLIXR::abort("Please define ILLIXR_DATA");
    }
    const std::string subpath     = "/" + spath + "/" + file_name;
    std::string       illixr_data = std::string{illixr_data_c_str};

    std::ifstream gt_file{illixr_data + subpath};

    if (!gt_file.good()) {
        spdlog::get("illixr")->error("[{0}] $ILLIXR_DATA{1} ({2}{1}{3}) is not a good path", plugin_name, subpath, illixr_data, file_name);
        ILLIXR::abort();
    }

    return func(gt_file, illixr_data + subpath);
}
