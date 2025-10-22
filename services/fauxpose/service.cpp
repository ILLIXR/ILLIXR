/*****************************************************************************/
/* fauxpose/plugin.cpp                                                       */
/*                                                                           */
/* Created: 03/03/2022                                                       */
/* Last Edited: 07/27/2023                                                   */
/*                                                                           */
/* An ILLIXR plugin that publishes position tracking data ("pose")           */
/*     from a mathematical operation just to quickly produce some known    */
/*     tracking path for the purpose of debugging other portions of ILLIXR.*/
/*                                                                           */
/* USAGE:                                                                   */
/*   * Add "- path: fauxpose/" as a plugin (and not other trackers)          */
/*   * Use "FAUXPOSE_PERIOD" environment variable to control orbit period    */
/*   * Use "FAUXPOSE_AMPLITUDE" environment variable to control orbit size   */
/*   * Use "FAUXPOSE_CENTER" env-var to control the center point of orbit    */
/*                                                                           */
/* TODO:                                                                     */
/*   * DONE: Need to implement as a "pose_prediction" "impl" (service?)      */
/*   * DONE: Fix so that "gldemo" (etc.) face forward.                       */
/*   * DONE: Implement environment variables to control parameters           */
/*   * Add control of the view direction as a control parameter              */
/*   * Add control of orbital plane as a control parameter                   */
/*                                                                           */
/* NOTES:                                                                    */
/*   * get_fast_pose() method returns a "fast_pose_type"                     */
/*   * "fast_pose_type" is a "pose_type" plus computed & target timestamps   */
/*   * correct_pose() method returns a "pose_type"                           */
/*   * (This version uploaded to ILLIXR GitHub)                              */
/*                                                                           */

#include "service.hpp"

#include <cstdlib>
#include <cstring>
#include <eigen3/Eigen/Core>
#include <eigen3/Eigen/Geometry>
#include <memory>
#include <mutex>

using namespace ILLIXR;
using namespace ILLIXR::data_format;

#if defined(_WIN32) || defined(_WIN64)

char* strchrnul(char* str, int c) {
    char* pos = strchr(str, c);
    return (pos != nullptr) ? pos : str + strlen(str);
}

const char* strchrnul(const char* str, int c) {
    const char* pos = strchr(str, c);
    return (pos != nullptr) ? pos : str + strlen(str);
}

#endif
fauxpose_impl::fauxpose_impl(const phonebook* pb)
    : switchboard_{pb->lookup_impl<switchboard>()}
    , clock_{pb->lookup_impl<relative_clock>()}
    , vsync_estimate_{switchboard_->get_reader<switchboard::event_wrapper<time_point>>("vsync_estimate")} {
    const char* env_input; /* pointer to environment variable input */
#ifndef NDEBUG
    spdlog::get("illixr")->debug("[fauxpose] Starting Service");
#endif

    // Store the initial time
    if (clock_->is_started()) {
        sim_start_time_ = clock_->now();
#ifndef NDEBUG
        spdlog::get("illixr")->debug("[fauxpose] Starting Service");
#endif
    } else {
#ifndef NDEBUG
        spdlog::get("illixr")->debug("[fauxpose] Warning: the clock isn't started yet");
#endif
    }

    // Set default faux-pose parameters
    center_location_ = Eigen::Vector3f{0.0, 1.5, 0.0};
    period_          = 0.5;
    amplitude_       = 2.0;

    // Adjust parameters based on environment variables
    if ((env_input = switchboard_->get_env_char("FAUXPOSE_PERIOD"))) {
        period_ = std::strtof(env_input, nullptr);
    }
    if ((env_input = switchboard_->get_env_char("FAUXPOSE_AMPLITUDE"))) {
        amplitude_ = std::strtof(env_input, nullptr);
    }
    if ((env_input = switchboard_->get_env_char("FAUXPOSE_CENTER"))) {
        center_location_[0] = std::strtof(env_input, nullptr);
        center_location_[1] = std::strtof(strchrnul(env_input, ',') + 1, nullptr);
        center_location_[2] = std::strtof(strchrnul(strchrnul(env_input, ',') + 1, ',') + 1, nullptr);
    }
#ifndef NDEBUG
    spdlog::get("illixr")->debug("[fauxpose] Period is {}", period_);
    spdlog::get("illixr")->debug("[fauxpose] Amplitude is {}", amplitude_);
    spdlog::get("illixr")->debug("[fauxpose] Center is {}, {}, {}", center_location_[0], center_location_[1],
                                 center_location_[2]);
#endif
}

fauxpose_impl::~fauxpose_impl() {
#ifndef NDEBUG
    spdlog::get("illixr")->debug("[fauxpose] Ending Service");
#endif
}

pose_type fauxpose_impl::correct_pose([[maybe_unused]] const pose_type& pose) const {
#ifndef NDEBUG
    spdlog::get("illixr")->debug("[fauxpose] Returning (passthru) pose");
#endif
    return pose;
}

Eigen::Quaternionf fauxpose_impl::get_offset() {
    return offset_;
}

void fauxpose_impl::set_offset(const Eigen::Quaternionf& raw_o_times_offset) {
    std::unique_lock   lock{offset_mutex_};
    Eigen::Quaternionf raw_o = raw_o_times_offset * offset_.inverse();
    offset_                  = raw_o.inverse();
}

fast_pose_type fauxpose_impl::get_fast_pose() const {
    // In actual pose prediction, the semantics are that we return
    //   the pose for next vsync, not now.
    switchboard::ptr<const switchboard::event_wrapper<time_point>> vsync_estimate = vsync_estimate_.get_ro_nullable();
    if (vsync_estimate == nullptr) {
        return get_fast_pose(clock_->now());
    } else {
        return get_fast_pose(vsync_estimate.get()->operator time_point());
    }
}

fast_pose_type fauxpose_impl::get_fast_pose(time_point time) const {
    pose_type simulated_pose; /* The algorithmically calculated 6-DOF pose */
    double    sim_time;       /* sim_time is used to regulate a consistent movement */

    RAC_ERRNO_MSG("[fauxpose] at start of _p_one_iteration");

    // Calculate simulation time from start of execution
    std::chrono::nanoseconds elapsed_time;
    elapsed_time = time - sim_start_time_;
    sim_time     = static_cast<double>(elapsed_time.count()) * 0.000000001;

    // Calculate new pose values
    //   Pose values are calculated from the passage of time to maintain consistency */
    simulated_pose.position[0] = static_cast<float>(center_location_[0] + amplitude_ * sin(sim_time * period_)); // X
    simulated_pose.position[1] = static_cast<float>(center_location_[1]);                                        // Y
    simulated_pose.position[2] = static_cast<float>(center_location_[2] + amplitude_ * cos(sim_time * period_)); // Z
    simulated_pose.orientation = Eigen::Quaternionf(0.707, 0.0, 0.707, 0.0); // (W,X,Y,Z) Facing forward (90deg about Y)

    // Return the new pose
#ifndef NDEBUG
    spdlog::get("illixr")->debug("[fauxpose] Returning pose");
#endif
    return fast_pose_type{simulated_pose, clock_->now(), time};
}

[[maybe_unused]] fauxpose::fauxpose(const std::string& name, phonebook* pb)
    : plugin{name, pb} {
    // "pose_prediction" is a class inheriting from "phonebook::service"
    //   It is described in "pose_prediction.hpp"
    pb->register_impl<pose_prediction>(std::static_pointer_cast<pose_prediction>(std::make_shared<fauxpose_impl>(pb)));
#ifndef NDEBUG
    spdlog::get("illixr")->debug("[fauxpose] Starting Plugin");
#endif
}

fauxpose::~fauxpose() {
#ifndef NDEBUG
    spdlog::get("illixr")->debug("[fauxpose] Ending Plugin");
#endif
}

// This line makes the plugin importable by Spindle
PLUGIN_MAIN(fauxpose)
