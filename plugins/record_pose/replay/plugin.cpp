#include "plugin.hpp"

#include <spdlog/spdlog.h>

using namespace ILLIXR;
using namespace ILLIXR::data_format;

[[maybe_unused]] pose_replay::pose_replay(const std::string& name, phonebook* pb)
    : threadloop{name, pb}
    , oxr_relay_type()
    , switchboard_{pb->lookup_impl<switchboard>()}
    , clock_{pb->lookup_impl<relative_clock>()}
#ifdef __ANDROID__
    , play_dummy_{switchboard_->get_env_bool("ILLIXR_DUMMY_INJECTOR", "false")}
        , topic_name_{(play_dummy_) ? "junk_pose" : "combined_pose"}
        , pose_writer_{switchboard_->get_network_writer<pose::combined_pose>(topic_name_, {
                .serialization_method=network::topic_config::BOOST, .transport_method=network::topic_config::UDP})}
        , latency_reader_{switchboard_->get_reader<network_latency_result>("network_latency")}
#else
    , pose_writer_{switchboard_->get_writer<pose::combined_pose>("combined_pose")}
#endif
    , in_file_{resolve_path(), std::ios::binary} {
#ifdef __ANDROID__
    if (!play_dummy_) {
#endif
    if (!in_file_.is_open())
        throw std::runtime_error("Could not open pose input file " + resolve_path());
    int32_t type_value = 0;
    in_file_.read(reinterpret_cast<char*>(&type_value), sizeof(type_value));
    capture_type_ = static_cast<pose::pose_capture_type>(type_value);

    switch (capture_type_) {
    case pose::pose_capture_type::FULL_POSE:
        read_func_ = &pose_replay::read_one_full_record;
        spdlog::get("illixr")->debug("[pose_replay]     opened pose file of type FULL");
        break;
    case pose::pose_capture_type::HEAD_ONLY_POSE:
        read_func_ = &pose_replay::read_one_head_record;
        spdlog::get("illixr")->debug("[pose_replay]     opened pose file of type HEAD");
        break;
    }

    in_file_.read(reinterpret_cast<char*>(&record_count_), sizeof(record_count_));

    archive_ = std::make_unique<boost::archive::binary_iarchive>(in_file_, boost::archive::no_header);
    front_chunk_ = load_chunk();
    if (!front_chunk_.empty()) {
        back_requested_ = true; // ask the loader thread to start prefetching as soon as it runs
    } else {
        reader_exhausted_ = true;
    }
    loader_thread_ = std::thread(&pose_replay::loader_thread_func, this);
#ifdef __ANDROID__
    }
#endif
}

pose_replay::~pose_replay() {
    {
        std::lock_guard<std::mutex> lock(reader_mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    if (loader_thread_.joinable()) {
        loader_thread_.join();
    }
}

threadloop::skip_option pose_replay::_p_should_skip() {

    return threadloop::skip_option::run;
}

void pose_replay::_p_one_iteration() {
#ifdef __ANDROID__
    if (play_dummy_) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        spdlog::get("illixr")->debug("[pose_replay] sleeping, playing dummy.");
        return;
    }
    auto now = std::chrono::steady_clock::now();
    if (!time_offsets_calibrated_ ||
        now - last_offset_calibration_ > std::chrono::seconds(30)) {
        calibrate_time_offsets();
        last_offset_calibration_ = now;
    }
    XrTime pose_time = predicted_time_.load();
#endif
    pose::combined_pose poses;
    time_point::duration record_time;
    if (!load_next(poses, record_time)) {
        return;
    }

#ifdef __ANDROID__
    auto data_now = time_point{std::chrono::duration<long, std::nano>{std::chrono::high_resolution_clock::now().time_since_epoch()}};
    // Store pose in history map so oxr_interface can correlate incoming
    // frames back to the original pose measurement via pose_id.
    {
        std::lock_guard<std::mutex> lock(pose_history_mutex_);
        pose_history_entry entry{};
        entry.pose           = poses.head_pose.pose;
        entry.generated_time = data_now;
        entry.xr_time        = pose_time;
        pose_history_[poses.id] = entry;

        // Prune oldest entries to keep the map bounded
        while (pose_history_.size() > MAX_POSE_HISTORY) {
            pose_history_.erase(pose_history_.begin());
        }
    }
    poses.pose_xr_time_ns = pose_time;
#endif
    poses.xr_to_monotonic_offset_ns = 0;
    poses.monotonic_to_system_offset_ns = monotonic_to_system_offset_ns_;

#ifdef __ANDROID__
    // Read the latest network latency data if available
    auto latency_data = latency_reader_.get_ro_nullable();

    if (latency_data != nullptr) {
        poses.smoothed_clock_offset_ns = latency_data->smoothed_clock_offset_ms * 1'000'000.0;
        poses.smoothed_rtt_ns = latency_data->smoothed_rtt_ms * 1'000'000.0;
    }
#else
    poses.smoothed_clock_offset_ns = 0;
    poses.smoothed_rtt_ns = 0;
#endif

    if (!initialized_) {
        playback_start_ = std::chrono::steady_clock::now();
        initialized_ = true;
    } else {
        std::this_thread::sleep_until(playback_start_ + record_time);
    }

    pose_writer_.put(pose_writer_.allocate<pose::combined_pose>(std::move(poses)));
}

std::string pose_replay::resolve_path() {
#ifdef __ANDROID__
    if (play_dummy_) {
        return "/dev/null";
    }
#endif
    std::string file_name = switchboard_->get_env("ILLIXR_POSE_INJECTOR_FILE");
    if (file_name.empty()) {
        spdlog::get("ILLIXR")->debug("Failed to open input file.");
        throw std::runtime_error("No output file specified for pose capture.");
    }
#ifdef __ANDROID__
    file_name = "/sdcard/Android/data/com.example.native_activity/" + file_name;
#endif
    spdlog::get("illixr")->debug("[pose_replay] Using pose file {}", file_name);
    return file_name;
}


pose_replay::chunk_type pose_replay::load_chunk() {
    spdlog::get("illixr")->debug("INJECTOR Loading chunk");
    chunk_type chunk;
    chunk.reserve(chunk_size_);
    ILLIXR::data_format::pose::combined_pose pose;
    time_point::duration                     timestamp_ms;
    while (chunk.size() < chunk_size_ && (this->*read_func_)(pose, timestamp_ms)) {
        chunk.emplace_back(pose, timestamp_ms);
    }
    return chunk;
}

void pose_replay::loader_thread_func() {
    while (true) {
        std::unique_lock<std::mutex> lock(reader_mutex_);
        cv_.wait(lock, [this] { return back_requested_ || stop_; });
        if (stop_) {
            return;
        }
        back_requested_ = false;
        lock.unlock();

        chunk_type chunk = load_chunk();

        std::lock_guard<std::mutex> result_lock(reader_mutex_);
        back_chunk_ = std::move(chunk);
        if (back_chunk_.empty()) {
            reader_exhausted_ = true;
        }
        back_ready_ = true;
        cv_.notify_all();
    }
}

bool pose_replay::load_next(ILLIXR::data_format::pose::combined_pose& pose, time_point::duration& timestamp_ms) {
    if (front_index_ >= front_chunk_.size()) {
        std::unique_lock<std::mutex> lock(reader_mutex_);
        cv_.wait(lock, [this] { return back_ready_ || reader_exhausted_; });

        if (front_chunk_.empty() && back_chunk_.empty()) {
            return false; // nothing was ever loaded, or file is fully exhausted
        }

        front_chunk_ = std::move(back_chunk_);
        back_chunk_.clear();
        front_index_ = 0;
        back_ready_  = false;
        const bool request_more = !reader_exhausted_;
        lock.unlock();

        if (front_chunk_.empty()) {
            return false;
        }
        if (request_more) {
            std::lock_guard<std::mutex> request_lock(reader_mutex_);
            back_requested_ = true;
            cv_.notify_all();
        }
    }

    pose         = front_chunk_[front_index_].first;
    timestamp_ms = front_chunk_[front_index_].second;
    ++front_index_;
    return true;
}

bool pose_replay::read_one_full_record(ILLIXR::data_format::pose::combined_pose& pose, time_point::duration& timestamp_ms) {
    if (read_record_count_ >= record_count_)
        return false;

    *archive_ >> pose;
    timestamp_ms = pose.head_pose.predict_computed_time.time_since_epoch();

    ++read_record_count_;
    return true;
}

bool pose_replay::read_one_head_record(ILLIXR::data_format::pose::combined_pose& pose, time_point::duration& timestamp_ms) {
    if (read_record_count_ >= record_count_)
        return false;

    pose::head_pose_capture capture_pose;
    *archive_ >> capture_pose;
    pose.head_pose.pose = capture_pose.pose;
    pose.head_pose.predict_computed_time = capture_pose.offset_time;
    pose.pose_xr_time_ns = capture_pose.target_time;
    pose.id = capture_pose.id;
    pose.xr_to_monotonic_offset_ns = capture_pose.xr_to_monotonic_offset_ns;
    pose.monotonic_to_system_offset_ns = capture_pose.monotonic_to_system_offset_ns;
    pose.smoothed_clock_offset_ns = capture_pose.smoothed_clock_offset_ns;
    pose.smoothed_rtt_ns = capture_pose.smoothed_rtt_ns;
    pose.valid_data = pose::HEAD_TRACKED;

    timestamp_ms = pose.head_pose.predict_computed_time.time_since_epoch();

    ++read_record_count_;
    return true;
}

void pose_replay::calibrate_time_offsets() {
#ifdef __ANDROID__
    // On Quest/Android, XrTime is nanoseconds since device boot,
    // equivalent to CLOCK_BOOTTIME. During an active XR session the
    // device does not suspend, so CLOCK_BOOTTIME == CLOCK_MONOTONIC.
    // We therefore approximate xr_to_monotonic_offset as zero and
    // compute monotonic_to_system_offset directly.
    //
    // Verification: sample XrTime from predicted_time_ alongside
    // CLOCK_MONOTONIC to confirm they are in the same timebase.
    xr_to_monotonic_offset_ns_ = 0;

    // Compute monotonic_to_system_offset_ns_:
    //   system_clock_ns = CLOCK_MONOTONIC_ns + monotonic_to_system_offset_ns_
    constexpr int NUM_SAMPLES = 20;
    int64_t best_offset = 0;
    int64_t best_rtt    = INT64_MAX;

    for (int i = 0; i < NUM_SAMPLES; i++) {
        auto before_sys = std::chrono::system_clock::now();
        struct timespec ts_mono{};
        clock_gettime(CLOCK_MONOTONIC, &ts_mono);
        auto after_sys = std::chrono::system_clock::now();

        int64_t before_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                before_sys.time_since_epoch()).count();
        int64_t after_ns  = std::chrono::duration_cast<std::chrono::nanoseconds>(
                after_sys.time_since_epoch()).count();
        int64_t rtt       = after_ns - before_ns;
        int64_t mid_ns    = before_ns + rtt / 2;
        int64_t mono_ns   = ts_mono.tv_sec * 1'000'000'000LL + ts_mono.tv_nsec;
        int64_t offset    = mid_ns - mono_ns;

        if (rtt < best_rtt) {
            best_rtt    = rtt;
            best_offset = offset;
        }
    }

    monotonic_to_system_offset_ns_ = best_offset;
#else
    monotonic_to_system_offset_ns_ = 0;
#endif
    time_offsets_calibrated_       = true;
}

#ifndef __ANDROID__
PLUGIN_MAIN(pose_replay)
#endif
