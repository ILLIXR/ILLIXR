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
    , fast_pose_{switchboard_->get_writer<fast_pose_type>("fast_pose")} {
    lighthouse_instance = this;
}

void lighthouse::stop() {
    threadloop::stop();
    survive_close(s_context_);
}

void lighthouse::process_slow_pose(SurviveObject* so, survive_long_timecode timecode, const SurvivePose* pose) {
    survive_default_pose_process(so, timecode, pose);

    auto quat = Eigen::Quaternionf{static_cast<float>(pose->Rot[0]), static_cast<float>(pose->Rot[1]),
                                   static_cast<float>(pose->Rot[2]), static_cast<float>(pose->Rot[3])};

    // The libsurvive coordinate system must be adjusted.
    auto adjustment = Eigen::Quaternionf{static_cast<float>(-sqrt(2.0)) / 2.f, static_cast<float>(sqrt(2.0)) / 2.f, 0.0, 0.0};
    auto new_quat   = adjustment * quat;
    new_quat.normalize();

    lighthouse_instance->slow_pose_.put(lighthouse_instance->slow_pose_.allocate(
        lighthouse_instance->clock_->now(), Eigen::Vector3d{pose->Pos[0], pose->Pos[2], -pose->Pos[1]}.cast<float>(),
        new_quat));

    lighthouse_instance->slow_pose_count_++;
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

// This line makes the plugin importable by Spindle
PLUGIN_MAIN(lighthouse)
