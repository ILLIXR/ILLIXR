#pragma once

#ifdef USING_OPENXR
#    include "illixr/data_format/poses/combined_pose.hpp"
#    define POSE_CAPTURE_TYPE ILLIXR::data_format::pose::combined_pose
#else
#    include "illixr/data_format/poses/head_pose.hpp"
#    define POSE_CAPTURE_TYPE ILLIXR::data_format::pose::fast_head_pose_type
#endif

#include "illixr/plugin.hpp"
#include "illixr/data_format/misc.hpp"
#include "illixr/switchboard.hpp"

#include <boost/archive/binary_oarchive.hpp>
#include <fstream>
#include <string>

namespace ILLIXR {
class pose_capture : public plugin {
public:
    [[maybe_unused]] pose_capture(const std::string& name, phonebook* pb);
    ~pose_capture() override;

    void process_pose(const switchboard::ptr<const POSE_CAPTURE_TYPE>& datum);

private:
    std::string resolve_path(bool head_pose_only);

    const std::shared_ptr<switchboard>  switchboard_;
    std::shared_ptr<relative_clock>  clock_;
    time_point::duration first_timestamp_;
    XrTime time_offset_;
    bool initialized_{false};

    std::ofstream out_pose_file_;
    std::ofstream out_head_pose_file_;

    std::unique_ptr<boost::archive::binary_oarchive>  pose_archive_;
    std::unique_ptr<boost::archive::binary_oarchive>  head_pose_archive_;

    std::streampos pose_count_posn_{0};
    std::streampos head_pose_count_posn_{0};

    uint64_t pose_record_count_{0};
    uint64_t head_pose_record_count_{0};
};

}
