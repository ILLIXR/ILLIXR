#pragma once

#ifndef ENABLE_MONADO
    #define ENABLE_MONADO
#endif

#define DOUBLE_INCLUDE
#ifdef USING_OPENXR
    #include "illixr/data_format/poses/combined_pose.hpp"
#else
    #include "illixr/data_format/poses/head_pose.hpp"
#endif

#include "illixr/data_format/pose_prediction.hpp"

#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"

#undef DOUBLE_INCLUDE

#include <map>

#ifdef USING_OPENXR
    #include "auxiliary/os/os_time.h"
#else
    #ifndef XrTime
typedef int64_t XrTime;
    #endif
#endif

namespace ILLIXR {

/// Entry stored in pose_map_, associating an extrapolated pose with the
/// id of the original combined_pose it was derived from.
struct pose_map_entry {
    uint64_t  id{0}; ///< combined_pose.id this pose was based on
    POSE_TYPE pose;
};

#ifdef USING_OPENXR
struct pose_point {
    XrTime             time;
    uint64_t           id; ///< combined_pose.id for this measurement
    xrt_space_relation pose;
};
#else
typedef ILLIXR::data_format::pose::fast_head_pose_type pose_point;
#endif

struct velocity_filter {
    static constexpr int MAX_WINDOW                  = 16;
    Eigen::Vector3f      linear_samples[MAX_WINDOW]  = {};
    Eigen::Vector3f      angular_samples[MAX_WINDOW] = {};
    int                  head                        = 0;
    int                  count                       = 0;
    bool                 initialized                 = false;
};

class MY_EXPORT_API pose_relay : public threadloop {
public:
    pose_relay(const std::string& name, phonebook* pb);

    /// Returns the most recent pose without any extrapolation.
    POSE_TYPE get_pose() const;

    /// Returns the extrapolated pose at the requested future time,
    /// accounting for full pipeline latency (network RTT + encode + decode).
    POSE_TYPE get_pose(POSE_TIME_TYPE future_time) const;

    bool fast_pose_reliable() const;

#ifdef USING_OPENXR
    /// Returns the combined_pose id associated with the extrapolated pose
    /// that was computed for the given Monado XrTime, or 0 if not found.
    /// Used by illixr_src_release to tag released frames with their source pose id.
    uint64_t get_pose_id_for_time(XrTime at_time) const;

    /// Search pose_map_ for the entry whose orientation most closely matches
    /// the given quaternion (by absolute dot product). Returns the combined_pose
    /// id of the best match, or 0 if no match is found above the threshold.
    /// Used by offload_rendering_server to tag compressed_frame::pose_id.
    uint64_t find_pose_id_by_orientation(const Eigen::Quaternionf& q) const;

    std::chrono::steady_clock::time_point get_pose_time(uint64_t id) const;
#endif

protected:
    threadloop::skip_option _p_should_skip() override;

    void _p_one_iteration() override;

private:
#ifdef USING_OPENXR
    void calibrate_monado_time_offset();
#endif
    std::shared_ptr<spdlog::logger> log_;
    std::shared_ptr<switchboard>    switchboard_;

#ifdef USING_OPENXR
    /**
     * @brief Reader for combined pose and hand tracking data from client.
     *
     * Contains the head pose, hand tracking data, and all time conversion
     * fields needed to bring the pose timestamp into Monado's timebase.
     */
    switchboard::reader<data_format::pose::combined_pose> combined_pose_;

    /**
     * @brief Writer for hand tracking data to Monado.
     *
     * Hand tracking data is extracted from combined_pose and published
     * to the "hand_poses" topic for Monado's ILLIXR driver to consume.
     * Mutable to allow calling from const get_pose().
     */
    mutable switchboard::writer<data_format::pose::hand_joint_poses_pair>       hand_tracking_writer_;
    mutable switchboard::writer<data_format::pose::hand_interaction_poses_pair> hand_interaction_writer_;
    mutable switchboard::writer<data_format::pose::palm_poses_pair>             palm_pose_writer_;
#else
    switchboard::reader<POSE_TYPE> render_pose_;
#endif

    // Pipeline latency constants (will be replaced with measured values later)
    static constexpr double ENCODE_LATENCY_NS = 9.0 * 1'000'000.0;
    static constexpr double DECODE_LATENCY_NS = 5.0 * 1'000'000.0;
    static constexpr double UNITY_LATENCY_NS  = 5.0 * 1'000'000.0;

/// Bounded history of poses in Monado timebase, oldest to newest.
#ifdef USING_OPENXR
    std::vector<pose_point> current_poses_;
    uint64_t                last_pose_id_ = 0;
#else
    POSE_TYPE current_pose_;
#endif

    /// Offset such that: monado_time_ns = os_monotonic_get_ns() timebase
    ///                   system_clock_ns = monado_time_ns + windows_epoch_offset_ns_
    /// Computed once at startup by calibrate_monado_time_offset().
    int64_t windows_epoch_offset_ns_  = 0;
    bool    monado_offset_calibrated_ = false;

    /// Cache of previously computed extrapolated poses keyed by XrTime,
    /// so repeated get_pose() calls for the same time avoid redundant work.
    /// Each entry carries the id of the combined_pose it was derived from
    /// so offload_rendering_server can correlate frames back to headset poses.
    /// Declared mutable so it can be populated from const get_pose().
    mutable std::map<XrTime, pose_map_entry> pose_map_;

    mutable std::mutex pose_mutex_;

    /// Most recent smoothed RTT in nanoseconds, updated each iteration
    /// from combined_pose. Atomic so get_pose() can read it without
    /// holding pose_mutex_.
    mutable std::atomic<double> smoothed_rtt_ns_{0.0};

    std::chrono::steady_clock::time_point                     last_offset_calibration_{};
    std::map<uint64_t, std::chrono::steady_clock::time_point> pose_time_{};
#ifdef USING_OPENXR
    bool use_hand_tracking_     = false;
    bool use_palm_poses_        = false;
    bool use_hand_interactions_ = false;
#endif
    bool            do_pose_prediction_{false};
    velocity_filter velocity_filter_;
    int             velocity_window_size_  = 8; // set from env in ctor
    float           velocity_deadband_lin_ = 0.05f;
    float           velocity_deadband_ang_ = 0.03f;
};

} // namespace ILLIXR
