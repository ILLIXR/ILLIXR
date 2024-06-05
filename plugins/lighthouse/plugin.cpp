#include <cassert>
#include <chrono>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <utility>

// ILLIXR includes
#include "illixr/data_format.hpp"
#include "illixr/error_util.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"
#include "survive.h"

using namespace ILLIXR;

class lighthouse;

lighthouse* lighthouse_instance;

class lighthouse : public threadloop {
public:
    lighthouse(const std::string& name_, phonebook* pb_)
        : threadloop{name_, pb_}
        , sb{pb->lookup_impl<switchboard>()}
        , log(spdlogger("info"))
        , _m_clock{pb->lookup_impl<RelativeClock>()}
        , _m_slow_pose{sb->get_writer<pose_type>("slow_pose")}
        , _m_fast_pose{sb->get_writer<fast_pose_type>("fast_pose")} {
        lighthouse_instance = this;
    }

    void stop() override {
        threadloop::stop();
        survive_close(ctx);
    }

    // destructor
    ~lighthouse() override { }

protected:
    static void process_slow_pose(SurviveObject* so, survive_long_timecode timecode, const SurvivePose* pose) {
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
        lighthouse_instance->_m_slow_pose.put(lighthouse_instance->_m_slow_pose.allocate(
            lighthouse_instance->_m_clock->now(), Eigen::Vector3d{pose->Pos[0], pose->Pos[2], -pose->Pos[1]}.cast<float>(),
            quat));

        lighthouse_instance->slow_pose_count++;
    }

    // static void process_fast_pose(SurviveObject* so, survive_long_timecode timecode, const SurvivePose* pose) {
    //     survive_default_imupose_process(so, timecode, pose);
    //
    //     auto quat = Eigen::Quaterniond{pose->Rot[0], pose->Rot[1], pose->Rot[2], pose->Rot[3]}.cast<float>();
    //
    //     lighthouse_instance->_m_fast_pose.put(lighthouse_instance->_m_fast_pose.allocate(
    //         pose_type {lighthouse_instance->_m_clock->now(), Eigen::Vector3d{pose->Pos[0], pose->Pos[1], pose->Pos[2]}.cast<float>(),
    //          quat},
    //         lighthouse_instance->_m_clock->now(), lighthouse_instance->_m_clock->now()));
    //
    //     lighthouse_instance->fast_pose_count++;
    // }

    void _p_thread_setup() override {
        ctx = survive_init(0, nullptr);

        survive_install_pose_fn(ctx, process_slow_pose);

        // survive_install_imupose_fn(ctx, process_fast_pose);
    }

    void _p_one_iteration() override {
        survive_poll(ctx);

        auto now = std::chrono::high_resolution_clock::now();
        auto dt = now - last_time;
        if (dt > std::chrono::seconds(1)) {
            log->info("slow pose rate: {} Hz", slow_pose_count);
            log->info("fast pose rate: {} Hz", fast_pose_count);
            slow_pose_count = 0;
            fast_pose_count = 0;
            last_time = now;
        }
    }

    skip_option _p_should_skip() override {
        return skip_option::run;
    }

private:
    const std::shared_ptr<spdlog::logger>      log;
    const std::shared_ptr<switchboard>         sb;
    const std::shared_ptr<const RelativeClock> _m_clock;
    switchboard::writer<pose_type>             _m_slow_pose;
    switchboard::writer<fast_pose_type>        _m_fast_pose;
    SurviveContext*                            ctx;

    std::chrono::time_point<std::chrono::high_resolution_clock> last_time;
    int slow_pose_count = 0;
    int fast_pose_count = 0;
};

// This line makes the plugin importable by Spindle
PLUGIN_MAIN(lighthouse)
