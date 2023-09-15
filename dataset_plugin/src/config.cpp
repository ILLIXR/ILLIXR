#include "include/config.hpp"

#include "common/error_util.hpp"

#include <cassert>  // for assert()
#include <iostream> // for std::cerr
#include <sstream>
#include <string>

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

void ConfigParser::initIMUConfig(Config& config) {
    // parsing timestamp unit-related info.
    const char* timestamp_units_env_var = std::getenv("ILLIXR_DATASET_IMU_TIMESTAMP_UNITS");
    if (!timestamp_units_env_var) {
        std::cerr << "Error: Please define `ILLIXR_DATASET_IMU_TIMESTAMP_UNITS`." << std::endl;
        ILLIXR::abort();
    }

    std::string timestamp_units{timestamp_units_env_var};

    if (timestamp_units == "seconds") {
        config.imu_config.timestamp_unit = TimestampUnit::second;
    } else if (timestamp_units == "milliseconds") {
        config.imu_config.timestamp_unit = TimestampUnit::millisecond;
    } else if (timestamp_units == "microseconds") {
        config.imu_config.timestamp_unit = TimestampUnit::microsecond;
    } else {
        // nanoseconds
        config.imu_config.timestamp_unit = TimestampUnit::nanosecond;
    }

    // parsing path-related info.
    const char* path_env_var = std::getenv("ILLIXR_DATASET_IMU_PATH");
    if (!path_env_var) {
        std::cerr << "Error: Please define `ILLIXR_DATASET_IMU_PATH`." << std::endl;
        ILLIXR::abort();
    }

    config.imu_config.path_list = convertPathStringToPathList(path_env_var);

    // parsing format-related info
    const char* format_env_var = std::getenv("ILLIXR_DATASET_IMU_FORMAT");
    if (!format_env_var) {
        std::cerr << "Error: Please define `ILLIXR_DATASET_IMU_FORMAT`." << std::endl;
        ILLIXR::abort();
    }

    std::string format{format_env_var};

    if (format == "true") {
        config.imu_config.format = true;
    } else {
        config.imu_config.format = false;
    }

    // sanity checking
    assert(config.imu_config.path_list.size() == config.imu_config.format.size());
}

void ConfigParser::initImageConfig(Config& config) {
    // parsing timestamp unit-related info.
    const char* timestamp_units_env_var = std::getenv("ILLIXR_DATASET_IMAGE_TIMESTAMP_UNITS");
    if (!timestamp_units_env_var) {
        std::cerr << "Error: Please define `ILLIXR_DATASET_IMAGE_TIMESTAMP_UNITS`." << std::endl;
        ILLIXR::abort();
    }

    std::string timestamp_units{timestamp_units_env_var};

    if (timestamp_units == "seconds") {
        config.image_config.timestamp_unit = TimestampUnit::second;
    } else if (timestamp_units == "milliseconds") {
        config.image_config.timestamp_unit = TimestampUnit::millisecond;
    } else if (timestamp_units == "microseconds") {
        config.image_config.timestamp_unit = TimestampUnit::microsecond;
    } else {
        // nanoseconds
        config.image_config.timestamp_unit = TimestampUnit::nanosecond;
    }

    // parsing image path-related info.

    // rgb path(s)
    const char* rgb_path_env_var = std::getenv("ILLIXR_DATASET_IMAGE_RGB_PATH");
    if (!rgb_path_env_var) {
        std::cerr << "Error: Please define `ILLIXR_DATASET_IMAGE_RGB_PATH`." << std::endl;
        ILLIXR::abort();
    }

    config.image_config.rgb_path_list = convertPathStringToPathList(rgb_path_env_var);

    // depth path(s)
    const char* depth_path_env_var = std::getenv("ILLIXR_DATASET_IMAGE_DEPTH_PATH");
    if (!depth_path_env_var) {
        std::cerr << "Error: Please define `ILLIXR_DATASET_IMAGE_DEPTH_PATH`." << std::endl;
        ILLIXR::abort();
    }

    config.image_config.depth_path_list = convertPathStringToPathList(depth_path_env_var);

    // grayscale path(s)
    const char* grayscale_path_env_var = std::getenv("ILLIXR_DATASET_IMAGE_GRAYSCALE_PATH");
    if (!grayscale_path_env_var) {
        std::cerr << "Error: Please define `ILLIXR_DATASET_IMAGE_GRAYSCALE_PATH`." << std::endl;
        ILLIXR::abort();
    }

    config.image_config.grayscale_path_list = convertPathStringToPathList(grayscale_path_env_var);
}

void ConfigParser::initPoseConfig(Config& config) {
    // parsing timestamp unit-related info.
    const char* timestamp_units_env_var = std::getenv("ILLIXR_DATASET_POSE_TIMESTAMP_UNITS");
    if (!timestamp_units_env_var) {
        std::cerr << "Error: Please define `ILLIXR_DATASET_POSE_TIMESTAMP_UNITS`." << std::endl;
        ILLIXR::abort();
    }

    std::string timestamp_units{timestamp_units_env_var};

    if (timestamp_units == "seconds") {
        config.pose_config.timestamp_unit = TimestampUnit::second;
    } else if (timestamp_units == "milliseconds") {
        config.pose_config.timestamp_unit = TimestampUnit::millisecond;
    } else if (timestamp_units == "microseconds") {
        config.pose_config.timestamp_unit = TimestampUnit::microsecond;
    } else {
        // nanoseconds
        config.pose_config.timestamp_unit = TimestampUnit::nanosecond;
    }

    // parsing path-related info.
    const char* path_env_var = std::getenv("ILLIXR_DATASET_POSE_PATH");
    if (!path_env_var) {
        std::cerr << "Error: Please define `ILLIXR_DATASET_POSE_PATH`." << std::endl;
        ILLIXR::abort();
    }

    config.pose_config.path_list = convertPathStringToPathList(path_env_var);
}

void ConfigParser::initGroundTruthConfig(Config& config) {
    // parsing timestamp unit-related info.
    const char* timestamp_units_env_var = std::getenv("ILLIXR_DATASET_GROUND_TRUTH_TIMESTAMP_UNITS");
    if (!timestamp_units_env_var) {
        std::cerr << "Error: Please define `ILLIXR_DATASET_GROUND_TRUTH_TIMESTAMP_UNITS`." << std::endl;
        ILLIXR::abort();
    }

    std::string timestamp_units{timestamp_units_env_var};

    if (timestamp_units == "seconds") {
        config.ground_truth_config.timestamp_unit = TimestampUnit::second;
    } else if (timestamp_units == "milliseconds") {
        config.ground_truth_config.timestamp_unit = TimestampUnit::millisecond;
    } else if (timestamp_units == "microseconds") {
        config.ground_truth_config.timestamp_unit = TimestampUnit::microsecond;
    } else {
        // nanoseconds
        config.ground_truth_config.timestamp_unit = TimestampUnit::nanosecond;
    }

    // parsing path-related info.
    const char* path_env_var = std::getenv("ILLIXR_GROUND_TRUTH_PATH");
    if (!path_env_var) {
        std::cerr << "Error: Please define `ILLIXR_GROUND_TRUTH_PATH`." << std::endl;
        ILLIXR::abort();
    }

    config.ground_truth_config.path = path_env_var;

    // parsing format-related info
    const char* format_env_var = std::getenv("ILLIXR_DATASET_GROUND_TRUTH_FORMAT");
    if (!format_env_var) {
        std::cerr << "Error: Please define `ILLIXR_DATASET_GROUND_TRUTH_FORMAT`." << std::endl;
        ILLIXR::abort();
    }

    // resetting the stringstream
    std::istringstream input_stream;
    std::string        number;

    while (std::getline(input_stream, number, ',')) {
        config.ground_truth_config.length_list.emplace_back(std::stoi(number));
    }

    // parsing name-related info
    const char* name_env_var = std::getenv("ILLIXR_DATASET_GROUND_TRUTH_NAMES");
    if (!name_env_var) {
        std::cerr << "Error: Please define `ILLIXR_DATASET_GROUND_TRUTH_NAMES`." << std::endl;
        ILLIXR::abort();
    }

    // resetting the stringstream
    input_stream.str("");
    input_stream.clear();
    std::string name;

    while (std::getline(input_stream, name, ',')) {
        config.ground_truth_config.name_list.emplace_back(name);
    }

    // sanity checking
    assert(config.ground_truth_config.length_list.size() == config.ground_truth_config.name_list.size());
}

void ConfigParser::initFromConfig(Config& config) {
    // delimiter
    const char* delimiter_env_var = std::getenv("ILLIXR_DATASET_DELIMITER");
    if (!delimiter_env_var) {
        std::cerr << "Error: Please define `ILLIXR_DATASET_DELIMITER`." << std::endl;
        ILLIXR::abort();
    }

    config.delimiter = delimiter_env_var[0];

    // root path
    const char* root_path_env_var = std::getenv("ILLIXR_DATASET_ROOT_PATH");
    if (!root_path_env_var) {
        std::cerr << "Error: Please define `ILLIXR_DATASET_ROOT_PATH`." << std::endl;
        ILLIXR::abort();
    }

    config.root_path = root_path_env_var;

    const char* use_imu_env_var = std::getenv("ILLIXR_DATASET_USE_IMU_PUBLISHER");
    if (use_imu_env_var) {
        initIMUConfig(config);
    }

    const char* use_image_env_var = std::getenv("ILLIXR_DATASET_USE_IMAGE_PUBLISHER");
    if (use_image_env_var) {
        initImageConfig(config);
    }

    const char* use_pose_env_var = std::getenv("ILLIXR_DATASET_USE_POSE_PUBLISHER");
    if (use_imu_env_var) {
        initPoseConfig(config);
    }

    const char* use_ground_truth_env_var = std::getenv("ILLIXR_DATASET_USE_GROUND_TRUTH_PUBLISHER");
    if (use_ground_truth_env_var) {
        initGroundTruthConfig(config);
    }
}
