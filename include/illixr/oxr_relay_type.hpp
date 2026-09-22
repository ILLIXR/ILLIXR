#pragma once

#include "illixr/data_format/poses/pose_base.hpp"
#include "illixr/relative_clock.hpp"

#include <mutex>
#include <openxr/openxr.h>

namespace ILLIXR {

/// Entry in the headset pose history map, used by oxr_interface to
/// correlate received frames back to the original pose measurement.
struct pose_history_entry {
    data_format::pose::xrt_space_relation pose;           ///< Head pose
    time_point                            generated_time; ///< Wall-clock time the pose was generated
    XrTime                                xr_time;        ///< predictedDisplayTime this pose was located for
};

class oxr_relay_type {
public:
    oxr_relay_type()                                                                             = default;
    virtual void initialize(XrInstance instance, XrSession session, XrSpace local, XrSpace view) = 0;

    void update_time(XrTime time) {
        predicted_time_.store(time);
    };

#ifdef __ANDROID__
    /// Look up a pose history entry by combined_pose id.
    /// Returns true and populates out_entry if found, false otherwise.
    /// Thread-safe.
    virtual bool get_pose_history(uint64_t id, pose_history_entry& out_entry) const {
        std::lock_guard<std::mutex> lock(pose_history_mutex_);
        auto                        it = pose_history_.find(id);
        if (it == pose_history_.end()) {
            return false;
        }
        out_entry = it->second;
        return true;
    }
#endif
    virtual void destroy() = 0;

    /// Whether XR_EXT_hand_tracking extension is supported
    bool hand_tracking_supported_{false};

    // ==================== Hand Interaction State ====================

    /// Whether XR_EXT_hand_interaction extension is supported and initialized
    bool hand_interaction_supported_{false};

protected:
    virtual void calibrate_time_offsets() = 0;
    // Map from combined_pose id to the pose that was sent with that id.
    // Bounded to 240 entries (~2 seconds at 120 Hz) to avoid unbounded growth.
    // Accessible by oxr_interface for frame correlation logging.
#ifdef __ANDROID__
    std::map<uint64_t, pose_history_entry> pose_history_;
#endif
    mutable std::mutex                     pose_history_mutex_;
    static constexpr size_t                MAX_POSE_HISTORY = 240;

    bool                                  time_offsets_calibrated_{false};
    XrTime                                monotonic_to_system_offset_ns_{0};
    XrTime                                xr_to_monotonic_offset_ns_{0};
    std::chrono::steady_clock::time_point last_offset_calibration_{};

    std::atomic<XrTime> predicted_time_ = 0;
};
} // namespace ILLIXR
