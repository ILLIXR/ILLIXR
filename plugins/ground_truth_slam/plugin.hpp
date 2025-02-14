#pragma once

#include "illixr/data_loading.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/plugin.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/data_format/pose.hpp"
#include "illixr/data_format/imu.hpp"

namespace ILLIXR {
// These are the first IMU timestamp of the datasets. See line#31 for more info.
#define ViconRoom1Easy      1403715273262142976
#define ViconRoom1Medium    1403715523912143104
#define ViconRoom1Difficult 1403715886544058112
#define ViconRoom2Easy      1413393212225760512
#define ViconRoom2Medium    1413393885975760384
#define ViconRoom2Hard      1413394881555760384

typedef data_format::pose_type sensor_types;

class ground_truth_slam : public plugin {
public:
    [[maybe_unused]] ground_truth_slam(const std::string& name, phonebook* pb);
    void start() override;
    void feed_ground_truth(const switchboard::ptr<const data_format::imu_type>& datum);

private:
    const std::shared_ptr<switchboard> switchboard_;
    switchboard::writer<data_format::pose_type>     true_pose_;

    switchboard::writer<switchboard::event_wrapper<Eigen::Vector3f>> ground_truth_offset_;
    const std::map<ullong, sensor_types>                             sensor_data_;
    ullong                                                           dataset_first_time_;
    bool                                                             first_time_;
};
} // namespace ILLIXR
