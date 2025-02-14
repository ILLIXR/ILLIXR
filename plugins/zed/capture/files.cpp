#include "files.hpp"

#include "illixr/data_format/pose.hpp"

#include <boost/filesystem.hpp>
#include <sstream>
//#include <string_view>

#define COMMA ","

using namespace ILLIXR::zed_capture;
const std::string d_file = "/data.csv";

boost::filesystem::path files::data_path_;
boost::filesystem::path files::img_path_;

boost::filesystem::path files::camL_path_;
boost::filesystem::path files::camR_path_;
// boost::filesystem::path files::depth_path;
// boost::filesystem::path files::conf_path;

std::string files::data_file_;
std::string files::camL_file_;
std::string files::camR_file_;
// std::string files::depth_file;
// std::string files::conf_file;
std::string files::cam_file_;

files* files::instance_ = nullptr;

files* files::getInstance() {
    if (instance_ == nullptr)
        throw std::runtime_error("Must be given a path");
    return instance_;
}

files* files::getInstance(const std::string& rt, const std::string& path) {
    if (instance_ == nullptr) {
        instance_ = new files(rt, path);
    }
    return instance_;
}

files::files(const std::string& rt, const std::string& sub_path) {
    root_ = rt;
    std::cout << root_ << "    " << sub_path << std::endl;
    data_path_ = root_ + sub_path + data_sub_path_;
    img_path_  = root_ + sub_path + img_sub_path_;

    camL_path_ = img_path_.string() + "/camL";
    camR_path_ = img_path_.string() + "/camR";
    // depth_path = img_path_.string() + "/depth";
    // conf_path = img_path_.string() + "/conf";

    data_file_ = data_path_.string() + d_file;
    camL_file_ = camL_path_.string() + d_file;
    camR_file_ = camR_path_.string() + d_file;
    // depth_file = depth_path.string() + d_file;
    // conf_file = conf_path.string() + d_file;
    cam_file_ = data_path_.string() + "/cam.csv";

    boost::filesystem::create_directories(data_path_);
    boost::filesystem::create_directories(camL_path_);
    boost::filesystem::create_directories(camR_path_);
    // boost::filesystem::create_directories(depth_path);
    // boost::filesystem::create_directories(conf_path);
}

std::ostream& operator<<(std::ostream& os, const ILLIXR::data_format::pose_type& dt) {
    os << dt.sensor_time.time_since_epoch().count() << COMMA << dt.position.x() << COMMA << dt.position.y() << COMMA
       << dt.position.z() << COMMA << dt.orientation.w() << COMMA << dt.orientation.x() << COMMA << dt.orientation.y() << COMMA
       << dt.orientation.z();
    return os;
}

std::ostream& operator<<(std::ostream& os, ILLIXR::data_format::ccd_data const& cd) {
    os << cd.center_x << COMMA << cd.center_y << COMMA << cd.vertical_fov << COMMA << cd.horizontal_fov;
    return os;
}

std::ostream& operator<<(std::ostream& os, ILLIXR::data_format::camera_data const& cc) {
    os << cc.width << COMMA << cc.height << COMMA << cc.fps << COMMA << cc.baseline << COMMA
       << cc.ccds.at(static_cast<const ILLIXR::data_format::units::eyes>(LEFT_EYE)) << COMMA
       << cc.ccds.at(static_cast<const ILLIXR::data_format::units::eyes>(RIGHT_EYE));
    return os;
}
