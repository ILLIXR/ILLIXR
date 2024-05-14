#include "illixr/plugin.hpp"

#include "illixr/data_format.hpp"
#include "illixr/opencv_data_types.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/switchboard.hpp"

#include <boost/filesystem.hpp>
#include <eigen3/Eigen/Dense>
#include <fstream>
#include <iomanip>
#include <memory>
#include <opencv2/imgcodecs.hpp>
#include <string>

using namespace ILLIXR;

class record_imu_cam : public plugin {
public:
    [[maybe_unused]] record_imu_cam(const std::string& name, phonebook* pb)
        : plugin{name, pb}
        , switchboard_{phonebook_->lookup_impl<switchboard>()}
        , cam_{switchboard_->get_buffered_reader<cam_type>("cam")}
        , record_data_{get_record_data_path()}
        , cam0_data_dir_{record_data_ / "cam0" / "data"}
        , cam1_data_dir_{record_data_ / "cam1" / "data"} {
        // check folder exist, if exist delete it
        boost::filesystem::remove_all(record_data_);

        // create imu0 directory
        boost::filesystem::path imu_dir = record_data_ / "imu0";
        boost::filesystem::create_directories(imu_dir);
        std::string imu_file = imu_dir.string() + "/data.csv";
        imu_wt_file_.open(imu_file, std::ofstream::out);
        imu_wt_file_ << "#timestamp [ns],w_x [rad s^-1],w_y [rad s^-1],w_z [rad s^-1],a_x [m s^-2],a_y [m s^-2],a_z [m s^-2]"
                     << std::endl;

        // create cam0 directory
        boost::filesystem::create_directories(cam0_data_dir_);
        std::string cam0_file = (record_data_ / "cam0" / "data.csv").string();
        cam0_wt_file_.open(cam0_file, std::ofstream::out);
        cam0_wt_file_ << "#timestamp [ns],filename" << std::endl;

        // create cam1 directory
        boost::filesystem::create_directories(cam1_data_dir_);
        std::string cam1_file = (record_data_ / "cam1" / "data.csv").string();
        cam1_wt_file_.open(cam1_file, std::ofstream::out);
        cam1_wt_file_ << "#timestamp [ns],filename" << std::endl;

        switchboard_->schedule<imu_type>(id_, "imu", [this](const switchboard::ptr<const imu_type>& datum, const std::size_t&) {
            this->dump_data(datum);
        });
    }

    void dump_data(const switchboard::ptr<const imu_type>& datum) {
        long            timestamp = datum->time.time_since_epoch().count();
        Eigen::Vector3d angular_v = datum->angular_v;
        Eigen::Vector3d linear_a  = datum->linear_a;

        // write imu0
        imu_wt_file_ << timestamp << "," << std::setprecision(17) << angular_v[0] << "," << angular_v[1] << "," << angular_v[2]
                     << "," << linear_a[0] << "," << linear_a[1] << "," << linear_a[2] << std::endl;

        // write cam0 and cam1
        switchboard::ptr<const cam_type> cam;
        cam                  = cam_.size() == 0 ? nullptr : cam_.dequeue();
        std::string cam0_img = cam0_data_dir_.string() + "/" + std::to_string(timestamp) + ".png";
        std::string cam1_img = cam1_data_dir_.string() + "/" + std::to_string(timestamp) + ".png";
        if (cam != nullptr) {
            cam0_wt_file_ << timestamp << "," << timestamp << ".png " << std::endl;
            cv::imwrite(cam0_img, cam->img0);
            cam1_wt_file_ << timestamp << "," << timestamp << ".png " << std::endl;
            cv::imwrite(cam1_img, cam->img1);
        }
    }

    ~record_imu_cam() override {
        imu_wt_file_.close();
        cam0_wt_file_.close();
        cam1_wt_file_.close();
    }

private:
    // TODO: This should come from a yaml file
    static boost::filesystem::path get_record_data_path() {
        boost::filesystem::path ILLIXR_DIR = boost::filesystem::current_path();
        return ILLIXR_DIR / "data_record";
    }

    std::ofstream                      imu_wt_file_;
    std::ofstream                      cam0_wt_file_;
    std::ofstream                      cam1_wt_file_;
    const std::shared_ptr<switchboard> switchboard_;

    switchboard::buffered_reader<cam_type> cam_;

    const boost::filesystem::path record_data_;
    const boost::filesystem::path cam0_data_dir_;
    const boost::filesystem::path cam1_data_dir_;
};

// This line makes the plugin importable by Spindle
PLUGIN_MAIN(record_imu_cam)
