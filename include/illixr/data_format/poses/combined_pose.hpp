#pragma once
#ifdef USING_OPENXR
#include "illixr/data_format/poses/head_pose.hpp"
#include "illixr/data_format/poses/hand_interaction_pose.hpp"
#include "illixr/data_format/poses/hand_pose.hpp"
#include "illixr/data_format/poses/palm_pose.hpp"

namespace ILLIXR::data_format::pose {

constexpr std::uint8_t HEAD_TRACKED         { 0b0000'0001 };
constexpr std::uint8_t HANDS_TRACKED        { 0b0000'0010 };
constexpr std::uint8_t PALMS_TRACKED        { 0b0000'0100 };
constexpr std::uint8_t INTERACTIONS_TRACKED { 0b0000'1000 };

struct combined_pose : public switchboard::event {
    fast_head_pose_type         head_pose;
    hand_joint_poses_pair       hand_poses;
    palm_poses_pair             palm_poses;
    hand_interaction_poses_pair hand_interactions;
    uint8_t                     valid_data = 0;
    uint64_t                    id = 0;

    // Time conversion data populated by oxr_relay on the headset.
    // These allow pose_relay on the server to convert the pose's XrTime
    // into Monado time without any additional cross-machine lookups.

    /// The XrTime (headset) at which the head pose was located.
    /// This is predictedDisplayTime from xrWaitFrame, in headset
    /// CLOCK_BOOTTIME nanoseconds (the headset OpenXR timebase).
    int64_t pose_xr_time_ns = 0;

    /// Offset from headset XrTime to headset CLOCK_MONOTONIC:
    ///   monotonic_ns = xr_time_ns + xr_to_monotonic_offset_ns
    /// Computed once via xrConvertTimespecTimeToTimeKHR at session start.
    int64_t xr_to_monotonic_offset_ns = 0;

    /// Offset from headset CLOCK_MONOTONIC to headset system_clock:
    ///   system_clock_ns = monotonic_ns + monotonic_to_system_offset_ns
    /// Computed once at session start by sampling both clocks together.
    int64_t monotonic_to_system_offset_ns = 0;

    /// Smoothed network clock offset (server system_clock - headset system_clock)
    /// in nanoseconds. Populated from network_latency_result by oxr_relay.
    /// Allows server to convert headset system_clock -> server system_clock.
    double smoothed_clock_offset_ns = 0.0;

    /// Smoothed round trip time in nanoseconds. Used by pose_relay to
    /// add transmission latency to the prediction target time.
    double smoothed_rtt_ns = 0.0;

    combined_pose() = default;

    combined_pose(fast_head_pose_type& head, hand_joint_poses_pair& hands, palm_poses_pair& palms,
                  hand_interaction_poses_pair& hand_interactions, uint64_t fid, int64_t pose_xr_time = 0,
                  int64_t xr_to_monotonic_offset = 0, int64_t monotonic_to_system_offset = 0, double smoothed_clock_offset = 0.,
                  double smoothed_rtt = 0.)
        : head_pose{head}
        , hand_poses{hands}
        , palm_poses{palms}
        , hand_interactions{hand_interactions}
        , id{fid}
        , pose_xr_time_ns{pose_xr_time}
        , xr_to_monotonic_offset_ns{xr_to_monotonic_offset}
        , monotonic_to_system_offset_ns{monotonic_to_system_offset}
        , smoothed_clock_offset_ns{smoothed_clock_offset}
        , smoothed_rtt_ns{smoothed_rtt} {
        if (head_pose.is_valid())
            valid_data |= HEAD_TRACKED;
        if (hand_poses.has_hands())
            valid_data |= HANDS_TRACKED;
        if (palm_poses.is_valid())
            valid_data |= PALMS_TRACKED;
        if (hand_interactions.is_valid())
            valid_data |= INTERACTIONS_TRACKED;
    }
};

} // namespace ILLIXR::data_format::pose

#endif
