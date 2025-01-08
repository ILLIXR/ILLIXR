#pragma once
#include "illixr/data_format/zed_cam.hpp"

#include <boost/filesystem.hpp>
#include <chrono>
#include <cstdint>
#include <eigen3/Eigen/Dense>
#include <iostream>
#include <map>

constexpr int LEFT_EYE  = 0;
constexpr int RIGHT_EYE = 1;

namespace ILLIXR::zed_capture {

typedef std::map<ILLIXR::data_format::image::image_type, cv::Mat>     img_map;
typedef std::map<ILLIXR::data_format::image::image_type, std::string> str_map;

class files {
public:
    files(const files&) = delete;
    files()             = delete;
    [[maybe_unused]] static files* getInstance();
    static files*                  getInstance(const std::string& rt, const std::string& path);

    static std::string data_file;
    static std::string camL_file;
    static std::string camR_file;
    // static std::string depth_file;
    // static std::string conf_file;
    static std::string cam_file;

    static boost::filesystem::path data_path;
    static boost::filesystem::path img_path;

    static boost::filesystem::path camL_path;
    static boost::filesystem::path camR_path;
    // static boost::filesystem::path depth_path;
    // static boost::filesystem::path conf_path;

private:
    const std::string data_sub_path = "data";
    const std::string img_sub_path  = "imgs";
    std::string       root;
    explicit files(const std::string& rt, const std::string& path);
    static files* instance;
};
} // namespace ILLIXR::zed_capture

std::ostream& operator<<(std::ostream& os, ILLIXR::data_format::pose_type const& dt);
std::ostream& operator<<(std::ostream& os, ILLIXR::data_format::camera_data const& cc);
