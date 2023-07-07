# pragma once

#include <cassert>    // for assert()
#include <chrono>     // for std::chrono::duration
#include <cstdlib>    // for std::getenv
#include <filesystem> // for std::filesystem::path
#include <string>
#include <vector>

// #include "common/data_format.hpp" // might not be necessary

// We assume that defaults are filled (to be done) and that all the environment variables exist.

struct IMUConfig {
    // datatype-related info
    bool use_double = true; // default is to use `double`
    std::chrono::duration timestamp_units;

    std::vector<std::filesystem::path> imu_path_list;

    // format-related info
    std::vector<bool> format;
    // if true, then linear acceleration is first. Else, angular velocity is first.
    // This was an arbitrary choice.
};

struct ImageConfig {
    std::chrono::duration timestamp_units;

    std::vector<std::filesystem::path> rgb_path_list;
    std::vector<std::filesystem::path> depth_path_list;
    std::vector<std::filesystem::path> grayscale_path_list;
};

struct PoseConfig {
    // datatype-related info
    bool use_double = false; // default is to use `float`
    std::chrono::duration timestamp_units;

    std::vector<std::filesystem::path> path_list;
};

struct GroundTruthConfig {
    // datatype-related info
    bool use_double = true; // default is to use `double`
    std::chrono::duration timestamp_units;
    std::filesystem::path path;
    
    // format-related info
    std::vector<int> length_list; // stores how long each group is
    std::vector<std::string> name_list; // stores the name of each group
    // should assert that length_list and name_list have the same length (after both have been filled)
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

class ConfigParser {
    public:
        ConfigParser() {
            initFromConfig();
        }
    
    private:
        // helper member functions
        void initIMUConfig();

        void initImageConfig();

        void initPoseConfig();

        void initGroundTruthConfig();
        
        void initFromConfig();

        Config config;
};
