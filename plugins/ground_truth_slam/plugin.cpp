#include "illixr/plugin.hpp"

#include "data_loading.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/switchboard.hpp"

#include <thread>
#include <utility>

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
        : plugin{std::move(name_), pb_}
        , sb{pb->lookup_impl<switchboard>()}
        , _m_true_pose{sb->get_writer<pose_type>("true_pose")}
        , _m_ground_truth_offset{sb->get_writer<switchboard::event_wrapper<Eigen::Vector3f>>("ground_truth_offset")}
        , _m_sensor_data{load_data()}
        // The relative-clock timestamp of each IMU is the difference between its dataset time and the IMU dataset_first_time.
        // Therefore we need the IMU dataset_first_time to reproduce the real dataset time.
        // TODO: Change the hardcoded number to be read from some configuration variables in the yaml file.
        , _m_dataset_first_time{ViconRoom1Medium}
        , _m_first_time{true} {
        spdlogger(std::getenv("GROUND_TRUTH_SLAM_LOG_LEVEL"));
    }

    void start() override {
        plugin::start();
        sb->schedule<imu_type>(id, "imu", [this](const switchboard::ptr<const imu_type>& datum, std::size_t) {
            this->feed_ground_truth(datum);
        });
    }

    void feed_ground_truth(const switchboard::ptr<const imu_type>& datum) {
        ullong rounded_time = datum->time.time_since_epoch().count() + _m_dataset_first_time;
        auto   it           = _m_sensor_data.find(rounded_time);
        if (it == _m_sensor_data.end()) {
#ifndef NDEBUG
            spdlog::get(name)->debug("True pose not found at timestamp: {}", rounded_time);
#endif
            return;
        }

        switchboard::ptr<pose_type> true_pose =
            _m_true_pose.allocate<pose_type>(pose_type{time_point{datum->time}, it->second.position, it->second.orientation});

#ifndef NDEBUG
        spdlog::get(name)->debug("Ground truth pose was found at T: {} | Pos: ({}, {}, {}) | Quat: ({}, {}, {}, {})",
                                 rounded_time, true_pose->position[0], true_pose->position[1], true_pose->position[2],
                                 true_pose->orientation.w(), true_pose->orientation.x(), true_pose->orientation.y(),
                                 true_pose->orientation.z());
#endif

        /// Ground truth position offset is the first ground truth position
        if (_m_first_time) {
            _m_first_time = false;
            _m_ground_truth_offset.put(
                _m_ground_truth_offset.allocate<switchboard::event_wrapper<Eigen::Vector3f>>(true_pose->position));
        }

        _m_true_pose.put(std::move(true_pose));
    }

private:
    const std::shared_ptr<switchboard> sb;
    switchboard::writer<pose_type>     _m_true_pose;

    switchboard::writer<switchboard::event_wrapper<Eigen::Vector3f>> _m_ground_truth_offset;
    const std::map<ullong, sensor_types>                             _m_sensor_data;
    ullong                                                           _m_dataset_first_time;
    bool                                                             _m_first_time;
};

PLUGIN_MAIN(ground_truth_slam);
