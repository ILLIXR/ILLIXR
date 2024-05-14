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
    [[maybe_unused]] ground_truth_slam(const std::string& name, phonebook* pb)
        : plugin{name, pb}
        , switchboard_{phonebook_->lookup_impl<switchboard>()}
        , true_pose_{switchboard_->get_writer<pose_type>("true_pose")}
        , ground_truth_offset_{switchboard_->get_writer<switchboard::event_wrapper<Eigen::Vector3f>>("ground_truth_offset")}
        , sensor_data_{load_data()}
        // The relative-clock timestamp of each IMU is the difference between its dataset time and the IMU dataset_first_time.
        // Therefore we need the IMU dataset_first_time to reproduce the real dataset time.
        // TODO: Change the hardcoded number to be read from some configuration variables in the yaml file.
        , dataset_first_time_{ViconRoom1Medium}
        , first_time_{true} {
        spdlogger(std::getenv("GROUND_TRUTH_SLAM_LOG_LEVEL"));
    }

    void start() override {
        plugin::start();
        switchboard_->schedule<imu_type>(id_, "imu", [this](const switchboard::ptr<const imu_type>& datum, std::size_t) {
            this->feed_ground_truth(datum);
        });
    }

    void feed_ground_truth(const switchboard::ptr<const imu_type>& datum) {
        ullong rounded_time = datum->time.time_since_epoch().count() + dataset_first_time_;
        auto   it           = sensor_data_.find(rounded_time);
        if (it == sensor_data_.end()) {
#ifndef NDEBUG
            spdlog::get(name_)->debug("True pose not found at timestamp: {}", rounded_time);
#endif
            return;
        }

        switchboard::ptr<pose_type> true_pose =
            true_pose_.allocate<pose_type>(pose_type{time_point{datum->time}, it->second.position, it->second.orientation});

#ifndef NDEBUG
        spdlog::get(name_)->debug("Ground truth pose was found at T: {} | Pos: ({}, {}, {}) | Quat: ({}, {}, {}, {})",
                                 rounded_time, true_pose->position[0], true_pose->position[1], true_pose->position[2],
                                 true_pose->orientation.w(), true_pose->orientation.x(), true_pose->orientation.y(),
                                 true_pose->orientation.z());
#endif

        /// Ground truth position offset is the first ground truth position
        if (first_time_) {
            first_time_ = false;
            ground_truth_offset_.put(
                ground_truth_offset_.allocate<switchboard::event_wrapper<Eigen::Vector3f>>(switchboard::event_wrapper<Eigen::Vector3f>(true_pose->position)));
        }

        true_pose_.put(std::move(true_pose));
    }

private:
    const std::shared_ptr<switchboard> switchboard_;
    switchboard::writer<pose_type>     true_pose_;

    switchboard::writer<switchboard::event_wrapper<Eigen::Vector3f>> ground_truth_offset_;
    const std::map<ullong, sensor_types>                             sensor_data_;
    ullong                                                           dataset_first_time_;
    bool                                                             first_time_;
};

PLUGIN_MAIN(ground_truth_slam)
