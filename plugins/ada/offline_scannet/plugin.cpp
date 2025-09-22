#include "plugin.hpp"

#include "illixr/data_loading.hpp"
#include "illixr/iterators/ssv_iterator.hpp"

#include <cassert>
#include <ratio>
#include <spdlog/spdlog.h>

using namespace ILLIXR;
using namespace ILLIXR::data_format;

inline std::map<ullong, sensor_types> read_data(std::ifstream& gt_file, const std::string& file_name) {
    std::map<ullong, sensor_types> data;
    //guard for npos
    const auto p = file_name.rfind("poses/");
    std::string illixr_data;
    if(p == std::string::npos){
        spdlog::get("illixr")->error("No pose is in the dataset.");
        ILLIXR::abort();
    }
    else{
        illixr_data = file_name.substr(0,p);
    }

    ullong idx = 0;
    for (ssv_iterator row{gt_file, 0}; row != ssv_iterator{}; ++row, ++idx) {
        Eigen::Vector3f    pose_position{std::stof(row[1]), std::stof(row[2]), std::stof(row[3])};
        Eigen::Quaternionf pose_orientation{std::stof(row[7]), std::stof(row[4]), std::stof(row[5]), std::stof(row[6])};
        sensor_types each_datapoint;
        each_datapoint.pose = {time_point{}, pose_position, pose_orientation};
        // Create lazy_load_image objects with loaded images instead of just storing paths
        each_datapoint.depth_cam = lazy_load_image{illixr_data + "/" + row[9]};
        each_datapoint.color_cam = lazy_load_image{illixr_data + "/" + row[11]};
        data.emplace(idx, std::move(each_datapoint));
    }
    if (!data.empty()) {
        auto last = data.rbegin();
        last->second.last_frame = true;
    }
    return data;
}

[[maybe_unused]] offline_scannet::offline_scannet(const std::string& name_, phonebook* pb_)
    : threadloop{name_, pb_}
    , switchboard_{phonebook_->lookup_impl<switchboard>()}
    , sensor_data_{load_data<sensor_types>("poses", "offline_scannet", &read_data, switchboard_, "groundtruth.txt")}
    , sensor_data_it_{sensor_data_.cbegin()}
    , clock_{phonebook_->lookup_impl<relative_clock>()} // this is for RGBA
    , scannet_{switchboard_->get_writer<data_format::scene_recon_type>("ScanNet_Data")} {
    try {
        frame_count_ = switchboard_->get_env_ulong("FRAME_COUNT", 0);
        spdlog::get("illixr")->info("offline_scannet: FRAME_COUNT is: {}", frame_count_);
    } catch (const std::invalid_argument& e) {
        spdlog::get("illixr")->error("Invalid argument: the environment variable is not a valid unsigned integer.");
    } catch (const std::out_of_range& e) {
        spdlog::get("illixr")->error(
            "Out of range: the value of the environment variable is too large for an unsigned integer.");
    }
    if (frame_count_ == 0)
        spdlog::get("illixr")->error("Environment variable not found.");

    spdlog::get("illixr")->info("data set finished loading start sending in 5");
    sleep(5);
}

threadloop::skip_option offline_scannet::_p_should_skip() {
    if (sensor_data_it_ != sensor_data_.end()) {
        // pyh publishing at 30Hz
        std::this_thread::sleep_for(std::chrono::duration<float, std::milli>(33.33));
        return skip_option::run;
    } else {
        return skip_option::stop;
    }
}

void offline_scannet::_p_one_iteration() {
    assert(sensor_data_it_ != sensor_data_.end());

    if (current_frame_count_ == 0) {
        spdlog::get("illixr")->info("sending start");
    }
    const sensor_types& sensor_datum = sensor_data_it_->second;
    ++sensor_data_it_;

    cv::Mat cam_depth = *(sensor_datum.depth_cam.load().release());

    if (cam_depth.empty()) {
        spdlog::get("illixr")->warn("depth not loaded");
    }
    cv::Mat cam_color_rgb = *(sensor_datum.color_cam.load().release());

    cv::Mat cam_color(cam_color_rgb.size(), CV_8UC4, cv::Scalar(0, 0, 0, 255));
    cv::cvtColor(cam_color_rgb, cam_color, cv::COLOR_RGB2RGBA, 0);
    if (cam_color.empty()) {
        spdlog::get("illixr")->warn("color not loaded");
    }

    data_format::pose_type pose = {time_point{}, sensor_datum.pose.position, sensor_datum.pose.orientation};

    scannet_.put(scannet_.allocate<scene_recon_type>(
        scene_recon_type{time_point{}, pose, cam_depth, cam_color, sensor_datum.last_frame}));

    if (sensor_datum.last_frame) {
        printf("sending last frame %u\n", current_frame_count_);
        spdlog::get("illixr")->info("finish sending the last frame");

    }
    printf("frame %u\n", current_frame_count_);
    if (current_frame_count_ == (frame_count_ - 30)) {
        printf("reaching last 30 frame: %u\n", current_frame_count_);
        spdlog::get("illixr")->info("reaching last 30 frame");
    }

    current_frame_count_++;
}

PLUGIN_MAIN(offline_scannet)
