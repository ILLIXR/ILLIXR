#include "plugin.hpp"

#include "illixr/error_util.hpp"
#include "illixr/phonebook.hpp"

#include <chrono>
#include <cmath>
#include <optional>
#include <string>

using namespace ILLIXR;
using namespace ILLIXR::data_format;

lighthouse* lighthouse_instance;

[[maybe_unused]] lighthouse::lighthouse(const std::string& name_, phonebook* pb_)
    : threadloop{name_, pb_}
    , switchboard_{phonebook_->lookup_impl<switchboard>()}
    , log_(spdlogger("info"))
    , clock_{phonebook_->lookup_impl<relative_clock>()}
    , slow_pose_{switchboard_->get_writer<pose_type>("slow_pose")}
    , fast_pose_{switchboard_->get_writer<fast_pose_type>("fast_pose")}
    , record_data_{get_record_data_path()} {
        lighthouse_instance = this;

        boost::filesystem::create_directories(record_data_);
        std::string pose_path = record_data_.string() + "/lighthouse_poses.csv";
        lighthouse_poses_file_.open(pose_path, std::ofstream::out);
        lighthouse_poses_file_ << "timestamp, x, y, z, q.w, q.x, q.y, q.z" << std::endl;
    }

void lighthouse::stop() {
    threadloop::stop();
    survive_close(s_context_);
}

void lighthouse::process_slow_pose(SurviveObject* so, survive_long_timecode timecode, const SurvivePose* pose) {
    survive_default_pose_process(so, timecode, pose);

    auto quat = Eigen::Quaternionf{pose->Rot[0], pose->Rot[1], pose->Rot[2], pose->Rot[3]};

    // The libsurvive coordinate system must be adjusted.
    auto adjustment = Eigen::Quaternionf{-sqrt(2.0) / 2.0, sqrt(2.0) / 2.0, 0.0, 0.0};
    auto new_quat   = adjustment * quat;
    new_quat.normalize();

    time_point now = lighthouse_instance->clock_->now();
    std::shared_ptr<pose_type> curr_pose = std::make_shared<pose_type>(now, now, Eigen::Vector3d{pose->Pos[0], pose->Pos[2], -pose->Pos[1]}.cast<float>(), new_quat);
    lighthouse_instance->slow_pose_.put(std::move(curr_pose));

    lighthouse_instance->slow_pose_count_++;
    lighthouse_instance->lighthouse_poses_file_ << curr_pose->cam_time.time_since_epoch().count() << ","
                               << curr_pose->position.x() << ","
                               << curr_pose->position.y() << ","
                               << curr_pose->position.z() << ","
                               << curr_pose->orientation.w() << ","
                               << curr_pose->orientation.x() << ","
                               << curr_pose->orientation.y() << ","
                               << curr_pose->orientation.z() << std::endl;

}

// static void process_fast_pose(SurviveObject* so, survive_long_timecode timecode, const SurvivePose* pose) {
//     survive_default_imupose_process(so, timecode, pose);
//
//     auto quat = Eigen::Quaterniond{pose->Rot[0], pose->Rot[1], pose->Rot[2], pose->Rot[3]}.cast<float>();
//
//     lighthouse_instance->_m_fast_pose.put(lighthouse_instance->_m_fast_pose.allocate(
//         pose_type {lighthouse_instance->_m_clock->now(), Eigen::Vector3d{pose->Pos[0], pose->Pos[1],
//         pose->Pos[2]}.cast<float>(),
//          quat},
//         lighthouse_instance->_m_clock->now(), lighthouse_instance->_m_clock->now()));
//
//     lighthouse_instance->fast_pose_count++;
// }

void lighthouse::_p_thread_setup() {
    s_context_ = survive_init(0, nullptr);

    survive_install_pose_fn(s_context_, process_slow_pose);

    // survive_install_imupose_fn(ctx, process_fast_pose);
}

void lighthouse::_p_one_iteration() {
    survive_poll(s_context_);

    auto now = std::chrono::high_resolution_clock::now();
    auto dt  = now - last_time_;
    if (dt > std::chrono::seconds(1)) {
        log_->info("slow pose rate: {} Hz", slow_pose_count_);
        log_->info("fast pose rate: {} Hz", fast_pose_count_);
        slow_pose_count_ = 0;
        fast_pose_count_ = 0;
        last_time_       = now;
    }
}

boost::filesystem::path lighthouse::get_record_data_path() {
    boost::filesystem::path ILLIXR_DIR = boost::filesystem::current_path();
    return ILLIXR_DIR / "lighthouse_poses";
}

// This line makes the plugin importable by Spindle
PLUGIN_MAIN(lighthouse)
