#include "plugin.hpp"

#include <filesystem>
#include <fstream>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

using namespace ILLIXR;
using namespace ILLIXR::data_format;

void data_injection::read_cam_data() {
    char          comma;
    std::ifstream data(data_root_path_ + "data/cam.csv", std::ifstream::in);
    size_t        w, h;
    std::string   line;
    float         fps, bl, lcx, lcy, lvf, lhf, rcx, rcy, rvf, rhf;
    data >> line; // get header
    data >> w >> comma >> h >> comma >> fps >> comma >> bl >> comma >> lcx >> comma >> lcy >> comma >> lvf >> comma >> lhf >>
        comma >> rcx >> comma >> rcy >> comma >> rvf >> comma >> rhf;

    data.close();
    ccd_map cmap = {{units::LEFT_EYE, {lcx, lcy, lvf, lhf}}, {units::RIGHT_EYE, {rcx, rcy, rvf, rhf}}};
    camera_data_ = {w, h, fps, bl, units::MILLIMETER, cmap};
}

void data_injection::read_poses() {
    std::string   line;
    char          comma;
    auto          temp = data_root_path_ + "data/data.csv";
    std::ifstream data(temp, std::ifstream::in);
    data >> line; // get header
    uint64_t t, tt;
    float    tx, ty, tz, w, x, y, z;
    base_time_ = 0;
    while (data) {
        data >> tt >> comma >> tx >> comma >> ty >> comma >> tz >> comma >> w >> comma >> x >> comma >> y >> comma >> z;
        if (base_time_ == 0)
            base_time_ = tt - 1;
        t = tt - base_time_;
        timepoints_.push_back(t);
        poses_[t] = new pose_data(Eigen::Vector3f{tx, ty, tx}, Eigen::Quaternionf{w, x, y, z}, units::MILLIMETER,
                                  coordinates::RIGHT_HANDED_Y_UP, coordinates::WORLD, 1.);
    }
}

void data_injection::load_images_on_the_fly() {
    data_injection::images_.clear();
    std::string f_root = std::to_string(base_time_ + timepoints_[current_]);
    std::string temp   = data_root_path_ + "imgs/camL/" + f_root + ".png";
    cv::Mat     tempi;
    if (std::filesystem::exists(data_root_path_ + "imgs/camL/" + f_root + ".png")) {
        tempi = cv::imread(temp); // data_root_path_ + "imgs/camL/" + f_root + ".png");
        cv::cvtColor(tempi, tempi, cv::COLOR_BGR2RGB);
        images_[image::LEFT_EYE] = tempi.clone();
    } else {
        std::cout << "FAIL " << f_root << std::endl;
    }
    temp = data_root_path_ + "imgs/camR/" + f_root + ".png";
    if (std::filesystem::exists(data_root_path_ + "imgs/camR/" + f_root + ".png")) {
        tempi = cv::imread(temp);
        cv::cvtColor(tempi, tempi, cv::COLOR_BGR2RGB);
        images_[image::RIGHT_EYE] = tempi.clone();
    }
}

data_injection::data_injection(const std::string& name_, phonebook* pb_)
    : threadloop{name_, pb_}
    , switchboard_{pb_->lookup_impl<switchboard>()}
    , frame_img_writer_{switchboard_->get_writer<binocular_cam_type>("cam")}
    , frame_pose_writer_{switchboard_->get_writer<pose_type>("pose")}
    , camera_data_writer_{switchboard_->get_writer<camera_data>("cam_data")}
    , counter_{0} {
    std::string test_data_root = switchboard_->get_env("ILLIXR_TEST_DATA");
    if (test_data_root.empty())
        throw std::runtime_error("No test data root specified");
    std::string test_data_root_str(test_data_root);
    data_root_path_ = test_data_root_str + "/";
    read_cam_data();
    read_poses();
    current_ = 0;
    offset_  = 0;
    step_    = timepoints_[1] - timepoints_[0];
}

void data_injection::start() {
    threadloop::start();
    camera_data_writer_.put(camera_data_writer_.allocate<camera_data>(camera_data{camera_data_}));
}

data_injection::~data_injection() {
    timepoints_.clear();
    for (auto p : poses_)
        delete p.second;
    poses_.clear();
    images_.clear();
    threadloop::~threadloop();
}

void data_injection::_p_one_iteration() {
    load_images_on_the_fly();
    frame_img_writer_.put(frame_img_writer_.allocate<binocular_cam_type>(
        binocular_cam_type{time_point{clock_duration_(timepoints_[current_] + offset_)}, images_.at(image::LEFT_EYE),
                           images_.at(image::RIGHT_EYE)}));
    frame_pose_writer_.put(frame_pose_writer_.allocate<pose_type>(
        pose_type{time_point{clock_duration_(timepoints_[current_] + offset_)}, *poses_.at(timepoints_[current_])}));
    current_++;
    if (current_ == timepoints_.size()) {
        current_ = 0;
        counter_++;
        offset_ = ((counter_ * timepoints_.size()) + 1) * step_;
    }
    std::this_thread::sleep_for(std::chrono::nanoseconds(step_));
}

PLUGIN_MAIN(data_injection)
