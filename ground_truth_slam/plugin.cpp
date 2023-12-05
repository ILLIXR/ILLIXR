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
#define ViconRoom1Easy      1403715273262142976
#define ViconRoom1Medium    1403715523912143104
#define ViconRoom1Difficult 1403715886544058112
#define ViconRoom2Easy      1413393212225760512
#define ViconRoom2Medium    1413393885975760384
#define ViconRoom2Hard      1413394881555760384

#define dataset_walking     1700613045229490665
#define dataset_static      1700611471945221229
#define dataset_bs          1700612128609292316

class ground_truth_slam : public threadloop {
public:
    ground_truth_slam(std::string name_, phonebook* pb_)
        : threadloop{name_, pb_}
        , sb{pb->lookup_impl<switchboard>()}
        , _m_true_pose{sb->get_writer<pose_type>("true_pose")}
        , _m_ground_truth_offset{sb->get_writer<switchboard::event_wrapper<Eigen::Vector3f>>("ground_truth_offset")}
        , _m_sensor_data{load_data()}
        , _m_sensor_data_it{_m_sensor_data.cbegin()}
        // The relative-clock timestamp of each IMU is the difference between its dataset time and the IMU dataset_first_time.
        // Therefore we need the IMU dataset_first_time to reproduce the real dataset time.
        // TODO: Change the hardcoded number to be read from some configuration variables in the yaml file.
        , _m_dataset_first_time{1700611471945221229}
        , _m_first_time{true} {
        if (!std::filesystem::exists(data_path)) {
            if (!std::filesystem::create_directory(data_path)) {
                std::cerr << "Failed to create data directory.";
            }
        }
        truth_csv.open(data_path + "/truth.csv");
        std::cout << "Number of ground truth poses " << _m_sensor_data.size() << std::endl;
    }

protected:
    virtual skip_option _p_should_skip() override {
        if (_m_sensor_data_it != _m_sensor_data.end())
            return skip_option::run;
        else
            return skip_option::stop;
    }

    virtual void _p_one_iteration() override {
        assert(_m_sensor_data_it != _m_sensor_data.end());
        time_point relative_time(std::chrono::duration<long, std::nano>{_m_sensor_data_it->first - _m_dataset_first_time});
        if (relative_time.time_since_epoch().count() < 0) {
            _m_sensor_data_it++;
            return;
        }
        switchboard::ptr<pose_type> true_pose = _m_true_pose.allocate<pose_type>(
                                                pose_type{relative_time,
                                                _m_sensor_data_it->second.position,
                                                _m_sensor_data_it->second.orientation});

        /// Ground truth position offset is the first ground truth position
        if (_m_first_time) {
            _m_first_time = false;
            _m_ground_truth_offset.put(
                _m_ground_truth_offset.allocate<switchboard::event_wrapper<Eigen::Vector3f>>(true_pose->position));
        }

        _m_true_pose.put(std::move(true_pose));
        std::cout << "One GT published\n";
        truth_csv << relative_time.time_since_epoch().count() << "," << true_pose->position.x() << "," << true_pose->position.y()
                  << "," << true_pose->position.z() << "," << true_pose->orientation.w() << "," << true_pose->orientation.x()
                  << "," << true_pose->orientation.y() << "," << true_pose->orientation.z() << std::endl;

        _m_sensor_data_it++;
    }

private:
    const std::shared_ptr<switchboard> sb;
    switchboard::writer<pose_type>     _m_true_pose;

    switchboard::writer<switchboard::event_wrapper<Eigen::Vector3f>> _m_ground_truth_offset;
    const std::map<ullong, sensor_types>                             _m_sensor_data;
    std::map<ullong, sensor_types>::const_iterator                   _m_sensor_data_it;
    ullong                                                           _m_dataset_first_time;
    bool                                                             _m_first_time;

    const std::string data_path = std::filesystem::current_path().string() + "/recorded_data";
    std::ofstream     truth_csv;
};

PLUGIN_MAIN(ground_truth_slam);
