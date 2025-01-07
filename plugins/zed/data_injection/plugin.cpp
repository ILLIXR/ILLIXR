#include "plugin.hpp"

#include <filesystem>
#include <fstream>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

using namespace ILLIXR;
using namespace ILLIXR::data_format;

void data_injection::read_cam_data() {
    char comma;
    std::ifstream data(_data_root_path + "data/cam.csv", std::ifstream::in);
    size_t w, h;
    std::string line;
    float fps, bl, lcx, lcy, lvf, lhf, rcx, rcy, rvf, rhf;
    data >> line; // get header
    data >> w >> comma >> h >> comma >> fps >> comma >> bl >> comma >> lcx >> comma >> lcy >> comma >> lvf >> comma >> lhf
         >> comma >> rcx >> comma >> rcy >> comma >> rvf >> comma >> rhf;

    data.close();
    ccd_map cmap = {{units::LEFT_EYE, {lcx, lcy, lvf, lhf}},
                    {units::RIGHT_EYE, {rcx, rcy, rvf, rhf}}};
    _camera_data = {w, h, fps, bl, units::MILLIMETER, cmap};
}

void data_injection::read_poses() {
    std::string line;
    char comma;
    auto temp = _data_root_path + "data/data.csv";
    std::ifstream data(temp, std::ifstream::in);
    data >> line;  // get header
    uint64_t t, tt;
    float tx, ty, tz, w, x, y, z;
    _base_time = 0;
    while(data) {
        data >> tt >> comma >> tx >> comma >> ty >> comma >> tz >> comma >> w >> comma >> x >> comma >> y >> comma >> z;
        if  (_base_time == 0)
            _base_time = tt - 1;
        t = tt - _base_time;
        _timepoints.push_back(t);
        _poses[t] = new pose_data(Eigen::Vector3f{tx, ty, tx}, Eigen::Quaternionf{w, x, y, z},
                                  units::MILLIMETER, coordinates::RIGHT_HANDED_Y_UP,
                                  coordinates::WORLD, 1.);
    }
}

void data_injection::load_images_on_the_fly() {
    data_injection::_images.clear();
    std::string f_root = std::to_string(_base_time + _timepoints[_current]);
    std::string temp = _data_root_path + "imgs/camL/" + f_root + ".png";
    cv::Mat tempi;
    if (std::filesystem::exists(_data_root_path + "imgs/camL/" + f_root + ".png")) {
        tempi = cv::imread(temp);//_data_root_path + "imgs/camL/" + f_root + ".png");
        cv::cvtColor(tempi, tempi, cv::COLOR_BGR2RGB);
        _images[image::LEFT_EYE] = tempi.clone();
    } else {
        std::cout << "FAIL " << f_root << std::endl;
    }
    temp = _data_root_path + "imgs/camR/" + f_root + ".png";
    if (std::filesystem::exists(_data_root_path + "imgs/camR/" + f_root + ".png")) {
        tempi = cv::imread(temp);
        cv::cvtColor(tempi, tempi, cv::COLOR_BGR2RGB);
        _images[image::RIGHT_EYE] = tempi.clone();
    }
}

data_injection::data_injection(const std::string &name_, phonebook *pb_)
        : threadloop{name_, pb_}
        , _switchboard{pb_->lookup_impl<switchboard>()}
        , _frame_img_writer{_switchboard->get_writer<binocular_cam_type>("cam")}
        , _frame_pose_writer{_switchboard->get_writer<pose_type>("pose")}
        , _camera_data_writer{_switchboard->get_writer<camera_data>("cam_data")}
        , _counter{0} {
    std::string test_data_root = _switchboard->get_env("ILLIXR_TEST_DATA");
    if (test_data_root.empty())
        throw std::runtime_error("No test data root specified");
    _data_root_path = test_data_root + "/";
    read_cam_data();
    read_poses();
    _current = 0;
    _offset = 0;
    _step = _timepoints[1] - _timepoints[0];
}

void data_injection::start() {
    threadloop::start();
    _camera_data_writer.put(_camera_data_writer.allocate<camera_data>(camera_data{_camera_data}));
}

data_injection::~data_injection() {
    _timepoints.clear();
    for (auto p : _poses)
        delete p.second;
    _poses.clear();
    _images.clear();
    threadloop::~threadloop();
}

void data_injection::_p_one_iteration() {
    load_images_on_the_fly();
    _frame_img_writer.put(_frame_img_writer.allocate<binocular_cam_type>(binocular_cam_type{time_point{_clock_duration(_timepoints[_current] + _offset)},
                                                                                            _images.at(image::LEFT_EYE),
                                                                                            _images.at(image::RIGHT_EYE)}));
    _frame_pose_writer.put(_frame_pose_writer.allocate<pose_type>(pose_type{time_point{_clock_duration(_timepoints[_current] + _offset)}, *_poses.at(_timepoints[_current])}));
    _current++;
    if (_current == _timepoints.size()) {
        _current = 0;
        _counter++;
        _offset = ((_counter * _timepoints.size()) + 1) * _step;
    }
    std::this_thread::sleep_for(std::chrono::nanoseconds(_step));
}

PLUGIN_MAIN(data_injection)
