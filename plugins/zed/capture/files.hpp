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

    static std::string data_file_;
    static std::string camL_file_;
    static std::string camR_file_;
    // static std::string depth_file;
    // static std::string conf_file;
    static std::string cam_file_;

    static boost::filesystem::path data_path_;
    static boost::filesystem::path img_path_;

    static boost::filesystem::path camL_path_;
    static boost::filesystem::path camR_path_;
    // static boost::filesystem::path depth_path;
    // static boost::filesystem::path conf_path;

private:
    explicit files(const std::string& rt, const std::string& path);

    const std::string data_sub_path_ = "data";
    const std::string img_sub_path_  = "imgs";
    std::string       root_;
    static files*     instance_;
};
} // namespace ILLIXR::zed_capture

std::ostream& operator<<(std::ostream& os, ILLIXR::data_format::pose_type const& dt);
std::ostream& operator<<(std::ostream& os, ILLIXR::data_format::camera_data const& cc);
