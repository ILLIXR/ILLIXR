#include "common/csv_iterator.hpp"

#include <eigen3/Eigen/Dense>
#include <filesystem>
#include <fstream>
#include <map>
#include <opencv2/core/mat.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <optional>
#include <string>

typedef unsigned long long ullong;

/*
 * Uncommenting this preprocessor macro makes the offline_cam load each data from the disk as it is needed.
 * Otherwise, we load all of them at the beginning, hold them in memory, and drop them in the queue as needed.
 * Lazy loading has an artificial negative impact on performance which is absent from an online-sensor system.
 * Eager loading deteriorates the startup time and uses more memory.
 */
//#define LAZY

class lazy_load_image {
public:
    lazy_load_image() { }

    lazy_load_image(const std::string& path)
        : _m_path(path) {
#ifndef LAZY
        _m_mat = cv::imread(_m_path, cv::IMREAD_GRAYSCALE);
#endif
    }

    lazy_load_image(const cv::Mat& _m_mat_data)
        : _m_mat(_m_mat_data) { }

    cv::Mat load() const {
#ifdef LAZY
        cv::Mat _m_mat = cv::imread(_m_path, cv::IMREAD_GRAYSCALE);
    #error "Linux scheduler cannot interrupt IO work, so lazy-loading is unadvisable."
#endif
        assert(!_m_mat.empty());
        return _m_mat;
    }

private:
    std::string _m_path;
    cv::Mat     _m_mat;
};

typedef struct {
    lazy_load_image cam0;
    lazy_load_image cam1;
} sensor_types;

static std::map<ullong, sensor_types> load_data() {
    const char* illixr_data_c_str = std::getenv("ILLIXR_DATA");
    if (!illixr_data_c_str) {
        std::cerr << "Please define ILLIXR_DATA" << std::endl;
        ILLIXR::abort();
    }
    std::string illixr_data = std::string{illixr_data_c_str};

    std::map<ullong, sensor_types> data;

    // First try to load the Kalibr folder of images if we have that...
    const std::string cam0_subpath_asl    = "/cam0/data.csv";
    const std::string cam0_subpath_kalibr = "/cam0/";
    std::ifstream     cam0_file_asl{illixr_data + cam0_subpath_asl};
    if (!cam0_file_asl.good() && std::filesystem::exists(illixr_data + cam0_subpath_kalibr) &&
        std::filesystem::is_directory(illixr_data + cam0_subpath_kalibr)) {
        for (const auto& entry : std::filesystem::directory_iterator(illixr_data + cam0_subpath_kalibr)) {
            ullong t     = std::stoull(std::filesystem::path(entry).filename().stem());
            data[t].cam0 = {entry.path()};
            data[t].cam1 = {cv::Mat()};
        }
    }

    // Otherwise fallback on using the data.csv file
    if (!cam0_file_asl.good() && data.empty()) {
        std::cerr << "${ILLIXR_DATA}" << cam0_subpath_asl << " (" << illixr_data << cam0_subpath_asl << ") is not a good path"
                  << std::endl;
        ILLIXR::abort();
    }
    for (CSVIterator row{cam0_file_asl, 1}; row != CSVIterator{}; ++row) {
        ullong t     = std::stoull(row[0]);
        data[t].cam0 = {illixr_data + "/cam0/data/" + row[1]};
        data[t].cam1 = {cv::Mat()};
    }

    // First try to load the Kalibr folder of images if we have that...
    const std::string cam1_subpath_asl    = "/cam1/data.csv";
    const std::string cam1_subpath_kalibr = "/cam1/";
    std::ifstream     cam1_file_asl{illixr_data + cam1_subpath_asl};
    if (!cam1_file_asl.good() && std::filesystem::exists(illixr_data + cam1_subpath_kalibr) &&
        std::filesystem::is_directory(illixr_data + cam1_subpath_kalibr)) {
        for (const auto& entry : std::filesystem::directory_iterator(illixr_data + cam1_subpath_kalibr)) {
            ullong t     = std::stoull(std::filesystem::path(entry).filename().stem());
            data[t].cam1 = {entry.path()};
        }
    }

    // Otherwise fallback on using the data.csv file
    if (cam1_file_asl.good()) {
        for (CSVIterator row{cam1_file_asl, 1}; row != CSVIterator{}; ++row) {
            ullong      t     = std::stoull(row[0]);
            std::string fname = row[1];
            data[t].cam1      = {illixr_data + "/cam1/data/" + row[1]};
        }
    }

    return data;
}
