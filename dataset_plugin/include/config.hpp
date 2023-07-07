# pragma once

#include <cassert>    // for assert()
#include <chrono>     // for std::chrono::duration
#include <cstdlib>    // for std::getenv
#include <filesystem> // for std::filesystem::path
#include <string>
#include <vector>

#include "common/data_format.hpp"

// const char* illixr_data_c_str = std::getenv("ILLIXR_DATA");
// if (!illixr_data_c_str) {
//     std::cerr << "Please define ILLIXR_DATA" << std::endl;
//     ILLIXR::abort();
// }
// std::string illixr_data = std::string{illixr_data_c_str};


// We assume that defaults are filled (to be done) and that all the environment variables exist.

struct IMUConfig {
    // datatype
    std::chrono::duration timestamp_units;

    // ...
};

struct ImageConfig {
    std::chrono::duration timestamp_units;

    std::vector<std::filesystem::path> rgb_path_list;
    std::vector<std::filesystem::path> depth_path_list;
    std::vector<std::filesystem::path> grayscale_path_list;
};

struct PoseConfig {
    std::chrono::duration timestamp_units;

    std::vector<std::filesystem::path> path_list;
};

struct GroundTruthConfig {
    // datatype
    std::chrono::duration timestamp_units;
    std::filesystem::path path;
    
    // format stuff
    std::vector<int> length_list; // stores how long each group is
    std::vector<std::string> name_list; // stores the name of each group
    // assert that length_list and name_list have the same length after both have been filled
};

struct Config {
    // delimiting character in the data file
    char delimiter;
    
    // path to the directory containing the datasets
    std::filesystem::path root_path;
    
    IMUConfig imu_config;

    ImageConfig image_config;

    PoseConfig pose_config;

    GroundTruthConfig ground_truth_config;

};

// TODO: See if this common --> specific child architecture is necessary.
class CommonConfigParser {
    // has helper functions to query info about path, timestamp units, and
    // datatype. This is common for IMU, Image (kinda), and Pose.
};

class IMUConfigParser : CommonConfigParser {

};

class ImageConfigParser : CommonConfigParser {

};

class PoseConfigParser : CommonConfigParser {

};

class GroundTruthConfigParser : CommonConfigParser {

};

class ConfigParser {
    public:
        ConfigParser() {
            initFromConfig();
        }
    
    private:
        void initFromConfig() {

        }
};