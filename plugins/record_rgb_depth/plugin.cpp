#include "plugin.hpp"

#include <opencv2/imgcodecs.hpp>

using namespace ILLIXR;


[[maybe_unused]] record_rgb_depth::record_rgb_depth(const std::string& name, phonebook* pb)
    : plugin{name, pb}
    , switchboard_{phonebook_->lookup_impl<switchboard>()}
    , record_data_{get_record_data_path()}
    , rgb_data_dir_{record_data_ / "rgb" / "data"}
    , depth_data_dir_{record_data_ / "depth" / "data"} {
    // check folder exist, if exist delete it
    boost::filesystem::remove_all(record_data_);

    // create cam0 directory
    boost::filesystem::create_directories(rgb_data_dir_);
    std::string cam0_file = (record_data_ / "rgb" / "data.csv").string();
    rgb_wt_file_.open(cam0_file, std::ofstream::out);
    rgb_wt_file_ << "#timestamp [ns],filename" << std::endl;

    // create cam1 directory
    boost::filesystem::create_directories(depth_data_dir_);
    std::string cam1_file = (record_data_ / "depth" / "data.csv").string();
    depth_wt_file_.open(cam1_file, std::ofstream::out);
    depth_wt_file_ << "#timestamp [ns],filename" << std::endl;

    switchboard_->schedule<rgb_depth_type>(id_, "rgb_depth",
                                           [&](const switchboard::ptr<const rgb_depth_type>& datum, std::size_t) {
                                               this->dump_data(datum);
                                           });
}

void record_rgb_depth::dump_data(const switchboard::ptr<const rgb_depth_type>& datum) {
    if (!datum)
        return;
    long timestamp = datum->time.time_since_epoch().count();

    // write rgb and depth
    std::string rgb_img   = rgb_data_dir_.string() + "/" + std::to_string(timestamp) + ".png";
    std::string depth_img = depth_data_dir_.string() + "/" + std::to_string(timestamp) + ".png";
    rgb_wt_file_ << timestamp << "," << timestamp << ".png " << std::endl;
    cv::imwrite(rgb_img, datum->rgb);
    depth_wt_file_ << timestamp << "," << timestamp << ".png " << std::endl;
    cv::imwrite(depth_img, datum->depth);
}

record_rgb_depth::~record_rgb_depth() {
    rgb_wt_file_.close();
    depth_wt_file_.close();
}


// TODO: This should come from a yaml file
boost::filesystem::path record_rgb_depth::get_record_data_path() {
    boost::filesystem::path ILLIXR_DIR = boost::filesystem::current_path();
    return ILLIXR_DIR / "data_rgbd_record";
}


// This line makes the plugin importable by Spindle
PLUGIN_MAIN(record_rgb_depth)
