#pragma once

#ifdef USING_OPENXR
#    include "illixr/data_format/poses/combined_pose.hpp"
#    include "illixr/data_format/serialization/combined_pose.hpp"
#    define POSE_CAPTURE_TYPE ILLIXR::data_format::pose::combined_pose
#    include <openxr/openxr.h>
#else
#    include "illixr/data_format/poses/head_pose.hpp"
#    define POSE_CAPTURE_TYPE ILLIXR::data_format::pose::fast_head_pose_type
#endif

#include "illixr/data_format/misc.hpp"
#include "illixr/oxr_relay_type.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"

#ifdef __ANDROID__
#include "illixr/data_format/latency_data.hpp"
#endif

#include <boost/archive/binary_iarchive.hpp>
#include <condition_variable>
#include <fstream>
#include <string>

namespace ILLIXR {
class pose_replay : public threadloop, public oxr_relay_type {
public:
    [[maybe_unused]] pose_replay(const std::string& name, phonebook* pb);

    ~pose_replay() override;

    void initialize(XrInstance instance, XrSession session, XrSpace local, XrSpace view) override {};

    void destroy() override {};

protected:
    skip_option _p_should_skip() override;

    void _p_one_iteration() override;

    void calibrate_time_offsets() override;
private:
    using record_type = std::pair<ILLIXR::data_format::pose::combined_pose, time_point::duration>;
    using chunk_type   = std::vector<record_type>;

    std::string resolve_path();

    void loader_thread_func();

    /**
     * @brief Reads up to chunk_size_ framed records from file_.
     *
     * Only ever called from the loader thread. Stops early, without error,
     * on a truncated or missing trailing record.
     */
    chunk_type load_chunk();

    /**
     * @brief Retrieves the next pose in playback order.
     *
     * In steady state this only touches the currently loaded chunk and does
     * not block. At a chunk boundary it swaps in the already-prefetched next
     * chunk and kicks off loading of the chunk after that.
     *
     * @param pose         Filled with the deserialized pose on success.
     * @param timestamp_ms Filled with the record's millisecond offset on success.
     * @return true if a record was retrieved, false if the file is exhausted.
     */
    bool load_next(ILLIXR::data_format::pose::combined_pose& pose, time_point::duration& timestamp_ms);

    /**
     * @brief Reads one length-prefixed record from file_.
     *
     * @param pose         Filled with the deserialized pose on success.
     * @param timestamp_ms Filled with the record's millisecond offset on success.
     * @return true if a complete record was read, false if none remain.
     */
    bool read_one_full_record(ILLIXR::data_format::pose::combined_pose& pose, time_point::duration& timestamp_ms);
    bool read_one_head_record(ILLIXR::data_format::pose::combined_pose& pose, time_point::duration& timestamp_ms);

    const std::shared_ptr<switchboard> switchboard_;
    std::shared_ptr<relative_clock> clock_;

#ifdef __ANDROID__
    bool play_dummy_ = false;
    std::string topic_name_;
    switchboard::network_writer<data_format::pose::combined_pose> pose_writer_;
    /// Reader for network latency results published by network_latency_pong_rx.
    /// Used to populate smoothed_clock_offset_ns and smoothed_rtt_ns in
    /// each combined_pose so the server has up-to-date network timing data.
    switchboard::reader<data_format::network_latency_result> latency_reader_;
#else
    switchboard::writer<data_format::pose::combined_pose> pose_writer_;
#endif

    bool initialized_{false};

    std::ifstream in_file_;
    std::unique_ptr<boost::archive::binary_iarchive> archive_;
    uint64_t record_count_{0};
    uint64_t read_record_count_{0};

    data_format::pose::pose_capture_type capture_type_;
    bool (pose_replay::*read_func_)(ILLIXR::data_format::pose::combined_pose& pose, time_point::duration& timestamp_ms);

    chunk_type front_chunk_;
    chunk_type back_chunk_;
    size_t front_index_ = 0;
    size_t chunk_size_ = 750;
    bool back_ready_ = false;
    bool back_requested_ = false;
    bool reader_exhausted_ = false;
    bool stop_ = false;

    std::mutex reader_mutex_;
    std::condition_variable cv_;
    std::thread loader_thread_;

    std::chrono::steady_clock::time_point playback_start_;
};

}
