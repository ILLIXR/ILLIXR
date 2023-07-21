#include "config.hpp"

#include <cassert>  // for the assert()
#include <iostream> // for std::cerr
#include <string>
#include <stringstream>

void ConfigParser::initIMUConfig(const Config& config) {
    // parsing timestamp unit-related info.
    const char* timestamp_units_env_var = std::getenv("ILLIXR_DATASET_IMU_TIMESTAMP_UNITS");
    if (!timestamp_env_var) {
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

    std::istringstream input_stream(format_env_var);
    std::string        lin_accel_first;

    while (std::getline(input_stream, lin_accel_first, ',')) {
        if (lin_accel_first == "true") {
            config.imu_config.format.emplace_back(true);
        } else {
            config.imu_config.format.emplace_back(false);
        }
    }

    // sanity checking
    assert(config.imu_config.path_list.size() == config.imu_config.format.size());
}

void ConfigParser::initImageConfig(const Config& config) {
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

void ConfigParser::initPoseConfig(const Config& config) {
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

void ConfigParser::initGroundTruthConfig(const Config& config) {
    // parsing timestamp unit-related info.
    const char* timestamp_units_env_var = std::getenv("ILLIXR_DATASET_GROUND_TRUTH_TIMESTAMP_UNITS");
    if (!timestamp_env_var) {
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

void ConfigParser::initFromConfig(const Config& config) {
    // delimiter
    const char* delimiter_env_var = std::getenv("ILLIXR_DATASET_DELIMITER");
    if (!delimiter_env_var) {
        std::cerr << "Error: Please define `ILLIXR_DATASET_DELIMITER`." << std::endl;
        ILLIXR::abort();
    }

    config.delimiter = delimiter[0];

    // root path
    const char* root_path_env_var = std::getenv("ILLIXR_DATASET_ROOT_PATH");
    if (!root_path_env_var) {
        std::cerr << "Error: Please define `ILLIXR_DATASET_ROOT_PATH`." << std::endl;
        ILLIXR::abort();
    }

    config.root_path = root_path_env_var;

    initIMUConfig(config);

    initImageConfig(config);

    initPoseConfig(config);

    initGroundTruthConfig(config);
}
