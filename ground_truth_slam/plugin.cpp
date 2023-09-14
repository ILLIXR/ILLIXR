#include "common/plugin.hpp"

#include "common/data_format.hpp"
#include "common/switchboard.hpp"
#include "common/threadloop.hpp"
#include "data_loading.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <thread>

using namespace ILLIXR;
// These are the first IMU timestamp of the datasets. See line#31 for more info.
#define ViconRoom1Easy      1403715273262142976
#define ViconRoom1Medium    1403715523912143104
#define ViconRoom1Difficult 1403715886544058112
#define ViconRoom2Easy      1413393212225760512
#define ViconRoom2Medium    1413393885975760384
#define ViconRoom2Hard      1413394881555760384

class ground_truth_slam : public plugin {
public:
    ground_truth_slam(std::string name_, phonebook* pb_)
        : plugin{name_, pb_}
        , sb{pb->lookup_impl<switchboard>()}
        , _m_true_pose{sb->get_writer<pose_type>("true_pose")}
        , _m_ground_truth_offset{sb->get_writer<switchboard::event_wrapper<Eigen::Vector3f>>("ground_truth_offset")}
        , _m_sensor_data{load_data()}
        // The relative-clock timestamp of each IMU is the difference between its dataset time and the IMU dataset_first_time.
        // Therefore we need the IMU dataset_first_time to reproduce the real dataset time.
        // TODO: Change the hardcoded number to be read from some configuration variables in the yaml file.
        , _m_dataset_first_time{ViconRoom1Medium}
        , _m_first_time{true} {
#ifdef ILLIXR_OFFLOADING_LOGGING
        if (!std::filesystem::exists(data_path)) {
            if (!std::filesystem::create_directory(data_path)) {
                std::cerr << "Failed to create data directory.";
            }
        }
        truth_csv.open(data_path + "/truth.csv");
#endif
    }

    virtual void start() override {
        plugin::start();
        sb->schedule<imu_type>(id, "imu", [this](switchboard::ptr<const imu_type> datum, std::size_t) {
            this->feed_ground_truth(datum);
        });
    }

    void feed_ground_truth(switchboard::ptr<const imu_type> datum) {
        ullong rounded_time = datum->time.time_since_epoch().count() + _m_dataset_first_time;
        auto   it           = _m_sensor_data.find(rounded_time);
        if (it == _m_sensor_data.end()) {
#ifndef NDEBUG
            std::cout << "True pose not found at timestamp: " << rounded_time << std::endl;
#endif
            return;
        }

        switchboard::ptr<pose_type> true_pose =
            _m_true_pose.allocate<pose_type>(pose_type{time_point{datum->time}, it->second.position, it->second.orientation});

#ifndef NDEBUG
        std::cout << "Ground truth pose was found at T: " << rounded_time << " | "
                  << "Pos: (" << true_pose->position[0] << ", " << true_pose->position[1] << ", " << true_pose->position[2]
                  << ")"
                  << " | "
                  << "Quat: (" << true_pose->orientation.w() << ", " << true_pose->orientation.x() << ", "
                  << true_pose->orientation.y() << "," << true_pose->orientation.z() << ")" << std::endl;
#endif

        /// Ground truth position offset is the first ground truth position
        if (_m_first_time) {
            _m_first_time = false;
            _m_ground_truth_offset.put(
                _m_ground_truth_offset.allocate<switchboard::event_wrapper<Eigen::Vector3f>>(true_pose->position));
        }

        _m_true_pose.put(std::move(true_pose));
#ifdef ILLIXR_OFFLOADING_LOGGING
        truth_csv << datum->time.time_since_epoch().count() << "," << true_pose->position.x() << "," << true_pose->position.y()
                  << "," << true_pose->position.z() << "," << true_pose->orientation.w() << "," << true_pose->orientation.x()
                  << "," << true_pose->orientation.y() << "," << true_pose->orientation.z() << std::endl;
#endif
    }

private:
    const std::shared_ptr<switchboard> sb;
    switchboard::writer<pose_type>     _m_true_pose;

    switchboard::writer<switchboard::event_wrapper<Eigen::Vector3f>> _m_ground_truth_offset;
    const std::map<ullong, sensor_types>                             _m_sensor_data;
    ullong                                                           _m_dataset_first_time;
    bool                                                             _m_first_time;
#ifdef ILLIXR_OFFLOADING_LOGGING
    const std::string data_path = std::filesystem::current_path().string() + "/recorded_data";
    std::ofstream     truth_csv;
#endif
};

PLUGIN_MAIN(ground_truth_slam);
