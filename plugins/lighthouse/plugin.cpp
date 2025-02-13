#include "plugin.hpp"
#include <chrono>
#include <cmath>
#include <optional>
#include <string>

#include "illixr/error_util.hpp"
#include "illixr/phonebook.hpp"

using namespace ILLIXR;


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

    auto quat = Eigen::Quaterniond{pose->Rot[0], pose->Rot[1], pose->Rot[2], pose->Rot[3]}.cast<float>();

    // // convert to euler angles
    // auto euler = quat.toRotationMatrix().eulerAngles(0, 1, 2);
    //
    // // print in degrees
    // lighthouse_instance->log->info("slow pose: {}, {}, {}", euler[0] * 180 / M_PI, euler[1] * 180 / M_PI,
    //                                euler[2] * 180 / M_PI);

    // rotate 90 degrees around x
    quat = Eigen::AngleAxisf{-M_PI / 2, Eigen::Vector3f::UnitX()} * quat;
    lighthouse_instance->slow_pose_.put(lighthouse_instance->slow_pose_.allocate(
        lighthouse_instance->clock_->now(), Eigen::Vector3d{pose->Pos[0], pose->Pos[2], -pose->Pos[1]}.cast<float>(),
        quat));

    lighthouse_instance->slow_pose_count++;
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
    auto dt  = now - last_time;
    if (dt > std::chrono::seconds(1)) {
        log_->info("slow pose rate: {} Hz", slow_pose_count);
        log_->info("fast pose rate: {} Hz", fast_pose_count);
        slow_pose_count = 0;
        fast_pose_count = 0;
        last_time       = now;
    }
}




// This line makes the plugin importable by Spindle
PLUGIN_MAIN(lighthouse)
