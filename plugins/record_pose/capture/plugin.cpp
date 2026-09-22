#include "plugin.hpp"

#include "illixr/data_format/serialization/combined_pose.hpp"

#include <sstream>

using namespace ILLIXR;
using namespace ILLIXR::data_format;

pose_capture::pose_capture(const std::string& name, phonebook* pb)
    : plugin{name, pb}
    , switchboard_{pb->lookup_impl<switchboard>()}
    , clock_{pb->lookup_impl<relative_clock>()}
    , out_pose_file_{resolve_path(false), std::ios::binary | std::ios::trunc}
    , out_head_pose_file_{resolve_path(true), std::ios::binary | std::ios::trunc}{
    switchboard_->schedule<POSE_CAPTURE_TYPE>(id_,
#ifdef USING_OPENXR
        "combined_pose_capture",
#else
                                              "",
#endif
                                              [&](const switchboard::ptr<const POSE_CAPTURE_TYPE>& datum, size_t) {
                                                process_pose(datum);
                                              });
    pose::pose_capture_type full_capture = pose::pose_capture_type::FULL_POSE;
    out_pose_file_.write(reinterpret_cast<const char*>(&full_capture), sizeof(full_capture));
    pose_count_posn_ = out_pose_file_.tellp();
    out_pose_file_.write(reinterpret_cast<const char*>(&pose_record_count_), sizeof(pose_record_count_));
    out_pose_file_.flush();
    pose_archive_ = std::make_unique<boost::archive::binary_oarchive>(out_pose_file_, boost::archive::no_header);

    pose::pose_capture_type head_capture = pose::pose_capture_type::HEAD_ONLY_POSE;
    out_head_pose_file_.write(reinterpret_cast<const char*>(&head_capture), sizeof(head_capture));
    head_pose_count_posn_ = out_head_pose_file_.tellp();
    out_head_pose_file_.write(reinterpret_cast<const char*>(&head_pose_record_count_), sizeof(head_pose_record_count_));
    out_head_pose_file_.flush();
    head_pose_archive_ = std::make_unique<boost::archive::binary_oarchive>(out_head_pose_file_, boost::archive::no_header);
}

pose_capture::~pose_capture() {
    pose_archive_.reset();
    out_pose_file_.flush();
    head_pose_archive_.reset();
    out_head_pose_file_.flush();
}

void pose_capture::process_pose(const switchboard::ptr<const POSE_CAPTURE_TYPE>& datum) {
    POSE_CAPTURE_TYPE pose_record = *datum.get();
    spdlog::get("illixr")->debug("[pose_capture] processing pose {} with size {}", pose_record.id, sizeof(pose_record));
    pose::head_pose_capture head_capture;

    if(!initialized_) {
#ifdef USING_OPENXR
        first_timestamp_ = pose_record.head_pose.predict_computed_time.time_since_epoch();
        time_offset_ = pose_record.pose_xr_time_ns;
#else
        first_timestamp_ = pose_record.predict_compute_time.time_since_epoch();
#endif
        initialized_ = true;
    }

#ifdef USING_OPENXR
    head_capture.pose = pose_record.head_pose.pose;

    pose_record.head_pose.predict_computed_time -= first_timestamp_;
    head_capture.offset_time = pose_record.head_pose.predict_computed_time;

    pose_record.pose_xr_time_ns -= time_offset_;
    head_capture.target_time = pose_record.pose_xr_time_ns;
    head_capture.id = pose_record.id;

    head_capture.xr_to_monotonic_offset_ns = pose_record.xr_to_monotonic_offset_ns;
    head_capture.monotonic_to_system_offset_ns = pose_record.monotonic_to_system_offset_ns;
    head_capture.smoothed_clock_offset_ns = pose_record.smoothed_clock_offset_ns;
    head_capture.smoothed_rtt_ns = pose_record.smoothed_rtt_ns;
#else
    pose_record.predict_computed_time -= first_timestamp_;
#endif
    *pose_archive_ << pose_record;
    out_pose_file_.flush();

    ++pose_record_count_;
    const std::streampos resume_posn = out_pose_file_.tellp();
    out_pose_file_.seekp(pose_count_posn_);
    out_pose_file_.write(reinterpret_cast<const char*>(&pose_record_count_), sizeof(pose_record_count_));
    out_pose_file_.flush();
    out_pose_file_.seekp(resume_posn);

    *head_pose_archive_ << head_capture;
    out_head_pose_file_.flush();

    ++head_pose_record_count_;
    const std::streampos head_resume_posn = out_head_pose_file_.tellp();
    out_head_pose_file_.seekp(head_pose_count_posn_);
    out_head_pose_file_.write(reinterpret_cast<const char*>(&head_pose_record_count_), sizeof(head_pose_record_count_));
    out_head_pose_file_.flush();
    out_head_pose_file_.seekp(head_resume_posn);


    spdlog::get("illixr")->debug("[pose_capture]     processed pose {} with size of {}", pose_record.id, sizeof(pose_record));
}

std::string pose_capture::resolve_path(bool head_pose_only) {
    std::string file_name = switchboard_->get_env("ILLIXR_POSE_CAPTURE_FILE");
    if(file_name.empty())
        file_name = "iilixr_pose_capture";
    std::string suffix;
    if (head_pose_only) {
        suffix = ".hpose";
    } else {
        suffix = ".ipose";
    }
#ifdef __ANDROID__
    file_name = "/sdcard/Android/data/com.example.native_activity/" + file_name + suffix;
#endif

    return file_name;
}

PLUGIN_MAIN(pose_capture)
