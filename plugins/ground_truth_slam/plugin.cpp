#include "plugin.hpp"

#include <utility>

using namespace ILLIXR;

[[maybe_unused]] ground_truth_slam::ground_truth_slam(const std::string& name, phonebook* pb)
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

void ground_truth_slam::start() {
    plugin::start();
    switchboard_->schedule<imu_type>(id_, "imu", [this](const switchboard::ptr<const imu_type>& datum, std::size_t) {
        this->feed_ground_truth(datum);
    });
}

void ground_truth_slam::feed_ground_truth(const switchboard::ptr<const imu_type>& datum) {
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
        ground_truth_offset_.put(ground_truth_offset_.allocate<switchboard::event_wrapper<Eigen::Vector3f>>(
            switchboard::event_wrapper<Eigen::Vector3f>(true_pose->position)));
    }

    true_pose_.put(std::move(true_pose));
}

PLUGIN_MAIN(ground_truth_slam)
