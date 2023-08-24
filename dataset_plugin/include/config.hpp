#pragma once

#include <cassert>    // for assert()
#include <cstddef>    // for the std::size_t data type
#include <cstdlib>    // for std::getenv
#include <filesystem> // for std::filesystem::path
#include <string>
#include <sstream>
#include <vector>

// We assume that defaults are filled (to be done) and that all the environment variables exist.

enum class TimestampUnit { second, millisecond, microsecond, nanosecond };

struct IMUConfig {
    // timestamp units-related info
    TimestampUnit timestamp_unit;

    std::vector<std::filesystem::path> path_list;

    // format-related info
    std::vector<bool> format;
    // if true, then linear acceleration is first. Else, angular velocity is first.
    // This was an arbitrary choice.
};

struct ImageConfig {
    // timestamp units-related info
    TimestampUnit timestamp_unit;

    std::vector<std::filesystem::path> rgb_path_list;
    std::vector<std::filesystem::path> depth_path_list;
    std::vector<std::filesystem::path> grayscale_path_list;
};

struct PoseConfig {
    // timestamp units-related info
    TimestampUnit timestamp_unit;

    std::vector<std::filesystem::path> path_list;
};

struct GroundTruthConfig {
    // timestamp units-related info
    TimestampUnit timestamp_unit;

    std::filesystem::path path;

    // format-related info
    std::vector<std::size_t> length_list; // stores how long each group is
    std::vector<std::string> name_list;   // stores the name of each group
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

// helper function
std::vector<std::filesystem::path> convertPathStringToPathList(const std::string& path_string) {
    // TODO: Come up with a better, more descriptive name

    // Takes in a comma-separated string like "data" or "data/hand-pose,data/head-pose"
    // and returns a vector of filesystem paths. In the previous examples, we will get
    // just {"data"}, and {"data/hand-pose", "data/head-pose"}.

    std::istringstream input_stream(path_string);
    std::string        path;

    std::vector<std::filesystem::path> output;

    while (std::getline(input_stream, path, ',')) {
        output.emplace_back(path);
    }

    return output;
}

class ConfigParser {
public:
    ConfigParser() = default;

    void initFromConfig(Config&);

private:
    // helper member functions
    void initIMUConfig(Config&);

    void initImageConfig(Config&);

    void initPoseConfig(Config&);

    void initGroundTruthConfig(Config&);
};
