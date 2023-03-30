#include "common/plugin.hpp"

#include "common/data_format.hpp"
#include "common/switchboard.hpp"

#include <boost/filesystem.hpp>
#include <fstream>
#include <iomanip>
#include <opencv2/core/mat.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
using namespace ILLIXR;

class record_rgb_depth : public plugin {
public:
    record_rgb_depth(std::string name_, phonebook* pb_)
        : plugin{name_, pb_}
        , sb{pb->lookup_impl<switchboard>()}
        , record_data{get_record_data_path()}
        , rgb_data_dir{record_data / "rgb" / "data"}
        , depth_data_dir{record_data / "depth" / "data"} {
        // check folder exist, if exist delete it
        boost::filesystem::remove_all(record_data);

        // create cam0 directory
        boost::filesystem::create_directories(rgb_data_dir);
        std::string cam0_file = (record_data / "rgb" / "data.csv").string();
        rgb_wt_file.open(cam0_file, std::ofstream::out);
        rgb_wt_file << "#timestamp [ns],filename" << std::endl;

        // create cam1 directory
        boost::filesystem::create_directories(depth_data_dir);
        std::string cam1_file = (record_data / "depth" / "data.csv").string();
        depth_wt_file.open(cam1_file, std::ofstream::out);
        depth_wt_file << "#timestamp [ns],filename" << std::endl;

        sb->schedule<rgb_depth_type>(id, "rgb_depth", [&](switchboard::ptr<const rgb_depth_type> datum, std::size_t) {
            this->dump_data(datum);
        });
    }

    void dump_data(switchboard::ptr<const rgb_depth_type> datum) {
        if (!datum)
            return;
        long timestamp = datum->time.time_since_epoch().count();

        // write rgb and depth
        std::string rgb_img   = rgb_data_dir.string() + "/" + std::to_string(timestamp) + ".png";
        std::string depth_img = depth_data_dir.string() + "/" + std::to_string(timestamp) + ".png";
        rgb_wt_file << timestamp << "," << timestamp << ".png " << std::endl;
        cv::imwrite(rgb_img, datum->rgb);
        depth_wt_file << timestamp << "," << timestamp << ".png " << std::endl;
        cv::imwrite(depth_img, datum->depth);
    }

    virtual ~record_rgb_depth() override {
        rgb_wt_file.close();
        depth_wt_file.close();
    }

private:
    std::ofstream                      rgb_wt_file;
    std::ofstream                      depth_wt_file;
    const std::shared_ptr<switchboard> sb;

    const boost::filesystem::path record_data;
    const boost::filesystem::path rgb_data_dir;
    const boost::filesystem::path depth_data_dir;

    // TODO: This should come from a yaml file
    boost::filesystem::path get_record_data_path() {
        boost::filesystem::path ILLIXR_DIR = boost::filesystem::current_path();
        return ILLIXR_DIR / "data_rgbd_record";
    }
};

// This line makes the plugin importable by Spindle
PLUGIN_MAIN(record_rgb_depth);
