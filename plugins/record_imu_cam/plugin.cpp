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
#include <utility>

using namespace ILLIXR;

class record_imu_cam : public plugin {
public:
    record_imu_cam(std::string name_, phonebook* pb_)
        : plugin{std::move(name_), pb_}
        , sb{pb->lookup_impl<switchboard>()}
        , _m_cam{sb->get_buffered_reader<cam_type>("cam")}
        , record_data{get_record_data_path()}
        , cam0_data_dir{record_data / "cam0" / "data"}
        , cam1_data_dir{record_data / "cam1" / "data"} {
        // check folder exist, if exist delete it
        boost::filesystem::remove_all(record_data);

        // create imu0 directory
        boost::filesystem::path imu_dir = record_data / "imu0";
        boost::filesystem::create_directories(imu_dir);
        std::string imu_file = imu_dir.string() + "/data.csv";
        imu_wt_file.open(imu_file, std::ofstream::out);
        imu_wt_file << "#timestamp [ns],w_x [rad s^-1],w_y [rad s^-1],w_z [rad s^-1],a_x [m s^-2],a_y [m s^-2],a_z [m s^-2]"
                    << std::endl;

        // create cam0 directory
        boost::filesystem::create_directories(cam0_data_dir);
        std::string cam0_file = (record_data / "cam0" / "data.csv").string();
        cam0_wt_file.open(cam0_file, std::ofstream::out);
        cam0_wt_file << "#timestamp [ns],filename" << std::endl;

        // create cam1 directory
        boost::filesystem::create_directories(cam1_data_dir);
        std::string cam1_file = (record_data / "cam1" / "data.csv").string();
        cam1_wt_file.open(cam1_file, std::ofstream::out);
        cam1_wt_file << "#timestamp [ns],filename" << std::endl;

        sb->schedule<imu_type>(id, "imu", [this](const switchboard::ptr<const imu_type>& datum, std::size_t) {
            this->dump_data(datum);
        });
    }

    void dump_data(const switchboard::ptr<const imu_type>& datum) {
        long            timestamp = datum->time.time_since_epoch().count();
        Eigen::Vector3d angular_v = datum->angular_v;
        Eigen::Vector3d linear_a  = datum->linear_a;

        // write imu0
        imu_wt_file << timestamp << "," << std::setprecision(17) << angular_v[0] << "," << angular_v[1] << "," << angular_v[2]
                    << "," << linear_a[0] << "," << linear_a[1] << "," << linear_a[2] << std::endl;

        // write cam0 and cam1
        switchboard::ptr<const cam_type> cam;
        cam                  = _m_cam.size() == 0 ? nullptr : _m_cam.dequeue();
        std::string cam0_img = cam0_data_dir.string() + "/" + std::to_string(timestamp) + ".png";
        std::string cam1_img = cam1_data_dir.string() + "/" + std::to_string(timestamp) + ".png";
        if (cam != nullptr) {
            cam0_wt_file << timestamp << "," << timestamp << ".png " << std::endl;
            cv::imwrite(cam0_img, cam->img0);
            cam1_wt_file << timestamp << "," << timestamp << ".png " << std::endl;
            cv::imwrite(cam1_img, cam->img1);
        }
    }

    ~record_imu_cam() override {
        imu_wt_file.close();
        cam0_wt_file.close();
        cam1_wt_file.close();
    }

private:
    std::ofstream                      imu_wt_file;
    std::ofstream                      cam0_wt_file;
    std::ofstream                      cam1_wt_file;
    const std::shared_ptr<switchboard> sb;

    switchboard::buffered_reader<cam_type> _m_cam;

    const boost::filesystem::path record_data;
    const boost::filesystem::path cam0_data_dir;
    const boost::filesystem::path cam1_data_dir;

    // TODO: This should come from a yaml file
    static boost::filesystem::path get_record_data_path() {
        boost::filesystem::path ILLIXR_DIR = boost::filesystem::current_path();
        return ILLIXR_DIR / "data_record";
    }
};

// This line makes the plugin importable by Spindle
PLUGIN_MAIN(record_imu_cam)
