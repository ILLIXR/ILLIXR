#pragma once

// ILLIXR includes
#include "illixr/data_format/pose.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"
#include "survive.h"

#include <boost/filesystem.hpp>
#include <fstream>

namespace ILLIXR {

class lighthouse : public threadloop {
public:
    [[maybe_unused]] lighthouse(const std::string& name_, phonebook* pb_);
    void stop() override;
    // destructor
    ~lighthouse() override = default;

protected:
    static void process_slow_pose(SurviveObject* so, survive_long_timecode timecode, const SurvivePose* pose);
    void        _p_thread_setup() override;
    void        _p_one_iteration() override;

    skip_option _p_should_skip() override {
        return skip_option::run;
    }

private:
    const std::shared_ptr<switchboard>               switchboard_;
    const std::shared_ptr<spdlog::logger>            log_;
    const std::shared_ptr<const relative_clock>      clock_;
    switchboard::writer<data_format::pose_type>      slow_pose_;
    switchboard::writer<data_format::fast_pose_type> fast_pose_;
    SurviveContext*                                  s_context_;

    std::chrono::time_point<std::chrono::high_resolution_clock> last_time_;
    int                                                         slow_pose_count_ = 0;
    int                                                         fast_pose_count_ = 0;

    static boost::filesystem::path get_record_data_path();
    const boost::filesystem::path record_data_;
    std::ofstream lighthouse_poses_file_;
};

} // namespace ILLIXR
